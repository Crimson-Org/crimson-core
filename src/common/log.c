#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "log.h"

#define MAXIMUM_LOG_BYTES (1024 * 1024)
/* Logs may hold paths and names, so only this user reads them */
#define PRIVATE_FILE_MODE 0600
#define NANOSECONDS_PER_MICROSECOND 1000
#define DECIMAL_BASE 10
/* In /proc/self/stat the fields after the command name start two characters past its closing bracket */
/* The start time is the twentieth of those fields, in clock ticks */
#define STAT_FIELDS_OFFSET 2
#define STAT_START_TIME_FIELD 19

static FILE *session_log;
static GLogFunc previous_handler;
static gsize written_bytes;
static gint64 start_time;


/* Debug lines come only from the program itself, since GLib's network debugging alone would fill the log */
static void write_message(const char *domain, GLogLevelFlags level, const char *message, gpointer data) {
	g_log_default_handler(domain, level, message, NULL);
	if (!session_log || written_bytes >= MAXIMUM_LOG_BYTES)
		return;
	if (domain && (level & (G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO)))
		return;
	g_autoptr(GDateTime) now = g_date_time_new_now_local();
	g_autofree char *timestamp = g_date_time_format(now, "%FT%T%z");
	g_autofree char *line = g_strdup_printf("%s %s\n", timestamp, message);
	if (strlen(line) > MAXIMUM_LOG_BYTES - written_bytes)
		return;
	int bytes = fprintf(session_log, "%s", line);
	if (bytes > 0)
		written_bytes += bytes;
	fflush(session_log);
}


static gint64 get_boot_microseconds(void) {
	struct timespec now;
	clock_gettime(CLOCK_BOOTTIME, &now);
	return now.tv_sec * G_USEC_PER_SEC + now.tv_nsec / NANOSECONDS_PER_MICROSECOND;
}


/* Microseconds since boot when the process started, so startup times include loading the libraries */
static gint64 get_process_start(void) {
	g_autofree char *status = NULL;
	if (!g_file_get_contents("/proc/self/stat", &status, NULL, NULL) || !strrchr(status, ')'))
		return get_boot_microseconds();
	g_auto(GStrv) fields = g_strsplit(strrchr(status, ')') + STAT_FIELDS_OFFSET, " ", -1);
	if (g_strv_length(fields) <= STAT_START_TIME_FIELD)
		return get_boot_microseconds();
	return g_ascii_strtoll(fields[STAT_START_TIME_FIELD], NULL, DECIMAL_BASE) * G_USEC_PER_SEC / sysconf(_SC_CLK_TCK);
}


/* Rotates the current log to the previous one, and returns nothing when logging is not wanted here */
static FILE *open_log_file(const char *slug) {
	g_autofree char *executable = g_file_read_link("/proc/self/exe", NULL);
	if (!executable)
		return NULL;
	g_autofree char *directory = g_path_get_dirname(executable);
	g_autofree char *log_directory = g_build_filename(directory, "logs", NULL);
	if (!g_file_test(log_directory, G_FILE_TEST_IS_DIR))
		return NULL;
	g_autofree char *current_name = g_strdup_printf("%s-current.log", slug);
	g_autofree char *previous_name = g_strdup_printf("%s-previous.log", slug);
	g_autofree char *current_path = g_build_filename(log_directory, current_name, NULL);
	g_autofree char *previous_path = g_build_filename(log_directory, previous_name, NULL);
	if (g_file_test(current_path, G_FILE_TEST_EXISTS) && g_rename(current_path, previous_path) != 0)
		return NULL;
	FILE *file = g_fopen(current_path, "w");
	if (file)
		g_chmod(current_path, PRIVATE_FILE_MODE);
	return file;
}


void start_session_log(const char *slug) {
	/* The monotonic clock and boot time differ by any suspend, so the start is moved across by the gap */
	start_time = g_get_monotonic_time() - (get_boot_microseconds() - get_process_start());
	session_log = open_log_file(slug);
	if (!session_log)
		return;
	/* The default handler receives every log domain, so GTK and libadwaita warnings reach the log too */
	previous_handler = g_log_set_default_handler(write_message, NULL);
}


void stop_session_log(void) {
	if (previous_handler)
		g_log_set_default_handler(previous_handler, NULL);
	if (session_log)
		fclose(session_log);
	session_log = NULL;
	previous_handler = NULL;
}


void note_startup_moment(const char *moment) {
	gint64 milliseconds = (g_get_monotonic_time() - start_time) / G_TIME_SPAN_MILLISECOND;
	g_debug("%s after %" G_GINT64_FORMAT " ms", moment, milliseconds);
}

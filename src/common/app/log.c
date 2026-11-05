#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <fcntl.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "common/storage/key-store.h"
#include "common/app/log.h"

#define MAXIMUM_LOG_BYTES (1024 * 1024)
#define NANOSECONDS_PER_MICROSECOND 1000
#define DECIMAL_BASE 10
/* In /proc/self/stat the fields after the command name start two characters past its closing bracket */
/* The start time is the twentieth of those fields, in clock ticks */
#define STAT_FIELDS_OFFSET 2
#define STAT_START_TIME_FIELD 19
#define LOG_FULL_LINE "The log is full, so later messages only go to the system journal\n"

static FILE *session_log;
/* Worker threads log too, so writing and closing the file take turns */
static GMutex log_lock;
static gsize written_bytes;
static gboolean is_log_full;
static gint64 start_time;


static const char *get_level_name(GLogLevelFlags level) {
	if (level & G_LOG_LEVEL_ERROR)
		return "error";
	if (level & G_LOG_LEVEL_CRITICAL)
		return "critical";
	if (level & G_LOG_LEVEL_WARNING)
		return "warning";
	if (level & G_LOG_LEVEL_MESSAGE)
		return "message";
	if (level & G_LOG_LEVEL_INFO)
		return "info";
	return "debug";
}


static void write_line(const char *line) {
	if (strlen(line) <= MAXIMUM_LOG_BYTES - written_bytes) {
		int bytes = fprintf(session_log, "%s", line);
		if (bytes > 0)
			written_bytes += bytes;
	} else if (!is_log_full) {
		fprintf(session_log, LOG_FULL_LINE);
		is_log_full = TRUE;
	}
	fflush(session_log);
}


/* Every message passes here, GTK's structured ones too, and still reaches the terminal and the journal as before.
 * Debug lines come only from the program itself, since GLib's network debugging alone would fill the log */
static GLogWriterOutput write_message(GLogLevelFlags level, const GLogField *fields, gsize count, gpointer data) {
	const char *domain = NULL, *message = NULL;
	for (gsize index = 0; index < count; index++) {
		if (g_strcmp0(fields[index].key, "GLIB_DOMAIN") == 0)
			domain = fields[index].value;
		else if (g_strcmp0(fields[index].key, "MESSAGE") == 0)
			message = fields[index].value;
	}
	gboolean is_foreign_debug = domain && (level & (G_LOG_LEVEL_DEBUG | G_LOG_LEVEL_INFO));
	if (message && !is_foreign_debug) {
		g_autoptr(GDateTime) now = g_date_time_new_now_local();
		g_autofree char *timestamp = g_date_time_format(now, "%FT%T%z");
		g_autofree char *line = g_strdup_printf("%s %s%s%s%s %s\n", timestamp, get_level_name(level),
			domain ? " " : "", domain ? domain : "", ":", message);
		g_mutex_lock(&log_lock);
		if (session_log)
			write_line(line);
		g_mutex_unlock(&log_lock);
	}
	return g_log_writer_default(level, fields, count, data);
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
	/* The log is private from its first byte, and stays out of programs the app starts, such as a terminal's shell */
	int descriptor = g_open(current_path, O_WRONLY | O_CREAT | O_TRUNC, PRIVATE_FILE_MODE);
	if (descriptor < 0)
		return NULL;
	fcntl(descriptor, F_SETFD, FD_CLOEXEC);
	return fdopen(descriptor, "w");
}


void start_session_log(const char *slug) {
	/* The monotonic clock and boot time differ by any suspend, so the start is moved across by the gap */
	start_time = g_get_monotonic_time() - (get_boot_microseconds() - get_process_start());
	session_log = open_log_file(slug);
	if (session_log)
		g_log_set_writer_func(write_message, NULL, NULL);
}


gboolean has_session_log(void) {
	return session_log != NULL;
}


void stop_session_log(void) {
	g_mutex_lock(&log_lock);
	if (session_log)
		fclose(session_log);
	session_log = NULL;
	g_mutex_unlock(&log_lock);
}


void note_startup_moment(const char *moment) {
	gint64 milliseconds = (g_get_monotonic_time() - start_time) / G_TIME_SPAN_MILLISECOND;
	g_debug("%s after %" G_GINT64_FORMAT " ms", moment, milliseconds);
}

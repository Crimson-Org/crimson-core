#include <gio/gio.h>
#include <glib/gstdio.h>

#include "common/app/identity.h"
#include "common/storage/key-store.h"

#define PRIVATE_FOLDER_MODE 0700
/* Other files follow the user's umask, as files any program writes do */
#define SHARED_FILE_MODE 0666


char *get_config_file_path(const char *file_name) {
	return g_build_filename(g_get_user_config_dir(), get_application_identity()->slug, file_name, NULL);
}


char *get_state_file_path(const char *file_name) {
	return g_build_filename(g_get_user_state_dir(), get_application_identity()->slug, file_name, NULL);
}


char *get_cache_file_path(const char *file_name) {
	return g_build_filename(g_get_user_cache_dir(), get_application_identity()->slug, file_name, NULL);
}


char *get_data_file_path(const char *file_name) {
	return g_build_filename(g_get_user_data_dir(), get_application_identity()->slug, file_name, NULL);
}


void remove_old_files(const char *folder, guint days) {
	g_autoptr(GDir) directory = g_dir_open(folder, 0, NULL);
	gint64 oldest = g_get_real_time() / G_USEC_PER_SEC - (gint64)days * (G_TIME_SPAN_DAY / G_USEC_PER_SEC);
	const char *name;
	while (directory && (name = g_dir_read_name(directory))) {
		g_autofree char *path = g_build_filename(folder, name, NULL);
		GStatBuf status;
		if (g_stat(path, &status) == 0 && S_ISREG(status.st_mode) && status.st_mtime < oldest)
			g_remove(path);
	}
}


void mark_file_used(const char *path) {
	g_autoptr(GFile) file = g_file_new_for_path(path);
	guint64 now = (guint64)(g_get_real_time() / G_USEC_PER_SEC);
	g_file_set_attribute_uint64(file, G_FILE_ATTRIBUTE_TIME_MODIFIED, now, G_FILE_QUERY_INFO_NONE, NULL, NULL);
}


gboolean make_private_folder(const char *path) {
	return g_mkdir_with_parents(path, PRIVATE_FOLDER_MODE) == 0;
}


void make_file_private(const char *path) {
	g_chmod(path, PRIVATE_FILE_MODE);
}


GKeyFile *load_key_file(const char *path, GKeyFileFlags flags) {
	GKeyFile *file = g_key_file_new();
	g_autoptr(GError) error = NULL;
	/* A file that is there but cannot be read is replaced by the defaults on the next save, so that is said */
	if (!g_key_file_load_from_file(file, path, flags, &error) && !g_error_matches(error, G_FILE_ERROR, G_FILE_ERROR_NOENT))
		g_warning("%s could not be read, so the defaults are used: %s", path, error->message);
	return file;
}


gboolean save_text_file(const char *path, const char *text, gboolean is_private) {
	g_autofree char *directory = g_path_get_dirname(path);
	if (!make_private_folder(directory)) {
		g_warning("%s was not saved, since its folder could not be made", path);
		return FALSE;
	}
	g_autoptr(GError) error = NULL;
	int mode = is_private ? PRIVATE_FILE_MODE : SHARED_FILE_MODE;
	if (!g_file_set_contents_full(path, text, -1, G_FILE_SET_CONTENTS_CONSISTENT, mode, &error)) {
		g_warning("%s was not saved: %s", path, error->message);
		return FALSE;
	}
	return TRUE;
}


static gboolean write_key_file(GKeyFile *file, const char *path, gboolean is_private) {
	g_autofree char *text = g_key_file_to_data(file, NULL, NULL);
	return save_text_file(path, text, is_private);
}


gboolean save_key_file(GKeyFile *file, const char *path) {
	return write_key_file(file, path, FALSE);
}


gboolean save_private_key_file(GKeyFile *file, const char *path) {
	return write_key_file(file, path, TRUE);
}


int read_key_integer(GKeyFile *file, const char *group, const char *key, int fallback, int minimum, int maximum) {
	g_autoptr(GError) error = NULL;
	int value = g_key_file_get_integer(file, group, key, &error);
	return error ? fallback : CLAMP(value, minimum, maximum);
}


gboolean read_key_boolean(GKeyFile *file, const char *group, const char *key, gboolean fallback) {
	g_autoptr(GError) error = NULL;
	gboolean value = g_key_file_get_boolean(file, group, key, &error);
	return error ? fallback : value;
}

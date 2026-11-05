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


static gboolean write_key_file(GKeyFile *file, const char *path, int mode) {
	g_autofree char *directory = g_path_get_dirname(path);
	if (!make_private_folder(directory)) {
		g_warning("%s was not saved, since its folder could not be made", path);
		return FALSE;
	}
	gsize length = 0;
	g_autofree char *text = g_key_file_to_data(file, &length, NULL);
	g_autoptr(GError) error = NULL;
	if (!g_file_set_contents_full(path, text, (gssize)length, G_FILE_SET_CONTENTS_CONSISTENT, mode, &error)) {
		g_warning("%s was not saved: %s", path, error->message);
		return FALSE;
	}
	return TRUE;
}


gboolean save_key_file(GKeyFile *file, const char *path) {
	return write_key_file(file, path, SHARED_FILE_MODE);
}


gboolean save_private_key_file(GKeyFile *file, const char *path) {
	return write_key_file(file, path, PRIVATE_FILE_MODE);
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

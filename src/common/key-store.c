#include <glib/gstdio.h>

#include "identity.h"
#include "key-store.h"

#define PRIVATE_FOLDER_MODE 0700


char *get_config_file_path(const char *file_name) {
	return g_build_filename(g_get_user_config_dir(), get_application_identity()->slug, file_name, NULL);
}


char *get_state_file_path(const char *file_name) {
	return g_build_filename(g_get_user_state_dir(), get_application_identity()->slug, file_name, NULL);
}


gboolean make_private_folder(const char *path) {
	return g_mkdir_with_parents(path, PRIVATE_FOLDER_MODE) == 0;
}


void make_file_private(const char *path) {
	g_chmod(path, PRIVATE_FILE_MODE);
}


GKeyFile *load_key_file(const char *path, GKeyFileFlags flags) {
	GKeyFile *file = g_key_file_new();
	g_key_file_load_from_file(file, path, flags, NULL);
	return file;
}


void save_key_file(GKeyFile *file, const char *path) {
	g_autofree char *directory = g_path_get_dirname(path);
	g_autoptr(GError) error = NULL;
	if (!make_private_folder(directory) || !g_key_file_save_to_file(file, path, &error))
		g_warning("%s was not saved", path);
}


int read_key_integer(GKeyFile *file, const char *group, const char *key, int fallback, int minimum, int maximum) {
	g_autoptr(GError) error = NULL;
	int value = g_key_file_get_integer(file, group, key, &error);
	return error ? fallback : CLAMP(value, minimum, maximum);
}

#include <glib/gstdio.h>

#include "identity.h"
#include "key-store.h"


char *get_config_file_path(const char *file_name) {
	return g_build_filename(g_get_user_config_dir(), get_application_identity()->slug, file_name, NULL);
}


char *get_state_file_path(const char *file_name) {
	return g_build_filename(g_get_user_state_dir(), get_application_identity()->slug, file_name, NULL);
}


GKeyFile *load_key_file(const char *path, GKeyFileFlags flags) {
	GKeyFile *file = g_key_file_new();
	g_key_file_load_from_file(file, path, flags, NULL);
	return file;
}


void save_key_file(GKeyFile *file, const char *path) {
	g_autofree char *directory = g_path_get_dirname(path);
	g_autoptr(GError) error = NULL;
	if (g_mkdir_with_parents(directory, 0700) != 0 || !g_key_file_save_to_file(file, path, &error))
		g_warning("%s was not saved", path);
}


int read_key_integer(GKeyFile *file, const char *group, const char *key, int fallback, int minimum, int maximum) {
	g_autoptr(GError) error = NULL;
	int value = g_key_file_get_integer(file, group, key, &error);
	return error ? fallback : CLAMP(value, minimum, maximum);
}

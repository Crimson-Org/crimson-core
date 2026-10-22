#pragma once

#include <glib.h>

/* Settings live in the user's config folder and window layout in the state folder, both under the program name */
char *get_config_file_path(const char *file_name);
char *get_state_file_path(const char *file_name);

/* Returns an empty file when the path is missing or unreadable, so callers keep their defaults */
GKeyFile *load_key_file(const char *path, GKeyFileFlags flags);
void save_key_file(GKeyFile *file, const char *path);

int read_key_integer(GKeyFile *file, const char *group, const char *key, int fallback, int minimum, int maximum);

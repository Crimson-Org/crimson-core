#pragma once

#include <glib.h>

/* Files that hold sign ins, passwords or logs are for this user only */
#define PRIVATE_FILE_MODE 0600

/* Settings live in the user's config folder and window layout in the state folder, both under the program name */
char *get_config_file_path(const char *file_name);
char *get_state_file_path(const char *file_name);

/* Makes a folder only this user can open, with any missing parents, and says whether the folder is there now */
gboolean make_private_folder(const char *path);
/* Lets only this user read and write the file, such as a log or a saved sign in */
void make_file_private(const char *path);

/* Returns an empty file when the path is missing or unreadable, so callers keep their defaults */
GKeyFile *load_key_file(const char *path, GKeyFileFlags flags);
void save_key_file(GKeyFile *file, const char *path);

int read_key_integer(GKeyFile *file, const char *group, const char *key, int fallback, int minimum, int maximum);

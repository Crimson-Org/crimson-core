#pragma once

#include <glib.h>

/* Files that hold sign ins, passwords or logs are for this user only */
#define PRIVATE_FILE_MODE 0600

/* Each kind of file lives in the user's folder for it, under the program name: settings in config, window layout in
 * state, things that can be fetched again in cache, and the user's own collections in data */
char *get_config_file_path(const char *file_name);
char *get_state_file_path(const char *file_name);
char *get_cache_file_path(const char *file_name);
char *get_data_file_path(const char *file_name);

/* Makes a folder only this user can open, with any missing parents, and says whether the folder is there now */
gboolean make_private_folder(const char *path);
/* Lets only this user read and write the file, such as a log or a saved sign in */
void make_file_private(const char *path);

/* Returns an empty file when the path is missing or unreadable, so callers keep their defaults */
GKeyFile *load_key_file(const char *path, GKeyFileFlags flags);
/* Both write the whole file or nothing, and say why when they could not */
gboolean save_key_file(GKeyFile *file, const char *path);
/* For files only this user may read from their first byte, such as a saved sign in */
gboolean save_private_key_file(GKeyFile *file, const char *path);

int read_key_integer(GKeyFile *file, const char *group, const char *key, int fallback, int minimum, int maximum);
gboolean read_key_boolean(GKeyFile *file, const char *group, const char *key, gboolean fallback);

#include <string.h>

#include "common/text/paths.h"

#define HOME_MARK "~"


char *shorten_home_path(const char *path) {
	const char *home = g_get_home_dir();
	gsize length = strlen(home);
	if (g_str_has_prefix(path, home) && (path[length] == G_DIR_SEPARATOR || path[length] == 0))
		return g_strconcat(HOME_MARK, path + length, NULL);
	return g_strdup(path);
}

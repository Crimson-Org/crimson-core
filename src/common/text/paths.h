#pragma once

#include <glib.h>

/* A path as people write it, with the home folder as ~, such as "~/Projects" */
char *shorten_home_path(const char *path);

#pragma once

#include <glib.h>

/* A count grouped in thousands the way the user's language writes numbers, such as 1,234,567 or 1 234 567 */
char *format_count(guint64 count);

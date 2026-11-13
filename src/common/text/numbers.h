#pragma once

#include <glib.h>

/* A count grouped in thousands the way the user's language writes numbers, such as 1,234,567 or 1 234 567 */
char *format_count(guint64 count);
/* A play time or length in minutes and seconds, with hours when it has any, such as 3:42 or 1:02:03 */
char *format_play_time(double seconds);

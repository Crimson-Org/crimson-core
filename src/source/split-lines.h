#pragma once

#include <gtksourceview/gtksource.h>

/* GtkSourceView's parser spends seconds on each split line even with colours off, so once the buffer holds one it
 * loses its language and bracket matching, as minified code gains little from colours */
void keep_split_lines_plain(GtkSourceBuffer *buffer);

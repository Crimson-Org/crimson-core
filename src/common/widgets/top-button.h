#pragma once

#include <gtk/gtk.h>

/* Puts a round button over a scroller that rises in once the view is a screen down and glides it back to the top.
 * The returned widget holds the scroller and is what the page shows */
GtkWidget *add_top_button(GtkScrolledWindow *scroller, const char *tooltip);
/* Glides a page made by add_top_button back to its top, such as when its view is chosen again */
void glide_page_to_top(GtkWidget *page);

#pragma once

#include <gtk/gtk.h>

/* Turns mouse wheel steps into short eased glides in either direction, while touchpads keep their exact motion.
 * Ctrl and the wheel pass through untouched, so a program can zoom with them */
void add_smooth_scrolling(GtkScrolledWindow *scroller);

/* Eases the view to a vertical position, replacing any glide already running */
void glide_scroller(GtkScrolledWindow *scroller, double goal, guint duration);

/* Clicking selectable text focuses it, and a viewport would otherwise jump to show the whole focused label */
void set_scroll_child(GtkScrolledWindow *scroller, GtkWidget *child);

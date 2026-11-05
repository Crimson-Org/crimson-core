#pragma once

#include <gtk/gtk.h>

/* The mouse's side buttons run the back and forward actions anywhere in the window, such as "win.back", where
 * forward may be NULL */
void add_mouse_navigation(GtkWidget *window, const char *back_action, const char *forward_action);

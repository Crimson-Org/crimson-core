#pragma once

#include <adwaita.h>

/* Called with the width the sidebar was dragged to, once the drag ends, so it can be kept for the next launch */
typedef void (*SidebarResized)(int width, gpointer data);

/* Puts a handle on the edge between the split view's sidebar and content, which drags the sidebar between the two
 * widths, and returns the widget to place where the split view was */
GtkWidget *add_sidebar_handle(AdwOverlaySplitView *split, int width, int minimum, int maximum,
	SidebarResized resized, gpointer data);

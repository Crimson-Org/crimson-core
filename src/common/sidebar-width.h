#pragma once

#include <adwaita.h>

#include "panes.h"

/* Called with the width the sidebar was dragged to, once the drag ends, so it can be kept for the next launch */
typedef void (*SidebarResized)(int width, gpointer data);

/* Puts a handle on the edge between the split view's sidebar and content, which drags the sidebar between the two
 * widths, and returns the widget to place where the split view was */
GtkWidget *add_sidebar_handle(AdwOverlaySplitView *split, int width, int minimum, int maximum,
	SidebarResized resized, gpointer data);
/* The same handle for a navigation split view, which only shows while the sidebar is docked beside the content */
GtkWidget *add_navigation_sidebar_handle(AdwNavigationSplitView *split, int width, int minimum, int maximum,
	SidebarResized resized, gpointer data);
/* The same handle for panes, which only shows while they sit side by side */
GtkWidget *add_panes_sidebar_handle(CrimsonPanes *panes, int width, int minimum, int maximum, SidebarResized resized,
	gpointer data);

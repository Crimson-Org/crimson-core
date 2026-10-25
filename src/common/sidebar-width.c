#include "sidebar-width.h"

/* The handle is this wide, half over the sidebar and half over the content */
#define HANDLE_PIXELS 6

/* One split view's handle, the limits it keeps to and where a drag started */
typedef struct {
	/* An AdwOverlaySplitView, an AdwNavigationSplitView or panes */
	GtkWidget *split;
	GtkWidget *handle;
	int width, minimum, maximum, drag_start;
	SidebarResized resized;
	gpointer data;
} SidebarHandle;


static void set_width(SidebarHandle *sidebar, int width) {
	sidebar->width = CLAMP(width, sidebar->minimum, sidebar->maximum);
	if (ADW_IS_OVERLAY_SPLIT_VIEW(sidebar->split)) {
		adw_overlay_split_view_set_min_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(sidebar->split), sidebar->width);
		adw_overlay_split_view_set_max_sidebar_width(ADW_OVERLAY_SPLIT_VIEW(sidebar->split), sidebar->width);
	} else if (CRIMSON_IS_PANES(sidebar->split)) {
		crimson_panes_set_sidebar_width(CRIMSON_PANES(sidebar->split), sidebar->width);
	} else {
		adw_navigation_split_view_set_min_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(sidebar->split), sidebar->width);
		adw_navigation_split_view_set_max_sidebar_width(ADW_NAVIGATION_SPLIT_VIEW(sidebar->split), sidebar->width);
	}
	gtk_widget_set_margin_start(sidebar->handle, sidebar->width - HANDLE_PIXELS / 2);
}


/* A collapsed sidebar lies over the content, is hidden or fills the window as its own page, so only a docked one
 * can be dragged */
static void update_handle(SidebarHandle *sidebar) {
	if (ADW_IS_OVERLAY_SPLIT_VIEW(sidebar->split)) {
		AdwOverlaySplitView *split = ADW_OVERLAY_SPLIT_VIEW(sidebar->split);
		gtk_widget_set_visible(sidebar->handle, !adw_overlay_split_view_get_collapsed(split)
			&& adw_overlay_split_view_get_show_sidebar(split));
	} else if (CRIMSON_IS_PANES(sidebar->split)) {
		gtk_widget_set_visible(sidebar->handle, !crimson_panes_get_collapsed(CRIMSON_PANES(sidebar->split)));
	} else {
		gtk_widget_set_visible(sidebar->handle,
			!adw_navigation_split_view_get_collapsed(ADW_NAVIGATION_SPLIT_VIEW(sidebar->split)));
	}
}


/* The drag is watched on the overlay, which stays put, since the handle moves under the pointer as the sidebar
 * resizes and offsets measured on it would fall behind; it only starts on the handle itself */
static void start_drag(GtkGestureDrag *drag, double x, double y, SidebarHandle *sidebar) {
	GtkWidget *overlay = gtk_event_controller_get_widget(GTK_EVENT_CONTROLLER(drag));
	graphene_rect_t bounds;
	gboolean is_on_handle = gtk_widget_get_visible(sidebar->handle)
		&& gtk_widget_compute_bounds(sidebar->handle, overlay, &bounds)
		&& graphene_rect_contains_point(&bounds, &GRAPHENE_POINT_INIT((float)x, (float)y));
	if (!is_on_handle) {
		gtk_gesture_set_state(GTK_GESTURE(drag), GTK_EVENT_SEQUENCE_DENIED);
		return;
	}
	gtk_gesture_set_state(GTK_GESTURE(drag), GTK_EVENT_SEQUENCE_CLAIMED);
	sidebar->drag_start = sidebar->width;
}


static void follow_drag(GtkGestureDrag *drag, double x, double y, SidebarHandle *sidebar) {
	gboolean is_right_to_left = gtk_widget_get_direction(sidebar->handle) == GTK_TEXT_DIR_RTL;
	set_width(sidebar, sidebar->drag_start + (int)(is_right_to_left ? -x : x));
}


static void end_drag(GtkGestureDrag *drag, double x, double y, SidebarHandle *sidebar) {
	if (sidebar->resized)
		sidebar->resized(sidebar->width, sidebar->data);
}


static GtkWidget *attach_handle(GtkWidget *split, int width, int minimum, int maximum, SidebarResized resized,
	gpointer data) {
	SidebarHandle *sidebar = g_new0(SidebarHandle, 1);
	*sidebar = (SidebarHandle){split, gtk_box_new(GTK_ORIENTATION_VERTICAL, 0), width, minimum, maximum, 0,
		resized, data};
	gtk_widget_set_size_request(sidebar->handle, HANDLE_PIXELS, -1);
	gtk_widget_set_halign(sidebar->handle, GTK_ALIGN_START);
	gtk_widget_set_cursor_from_name(sidebar->handle, "col-resize");
	gtk_widget_add_css_class(sidebar->handle, "sidebar-handle");
	GtkGesture *drag = gtk_gesture_drag_new();
	g_signal_connect(drag, "drag-begin", G_CALLBACK(start_drag), sidebar);
	g_signal_connect(drag, "drag-update", G_CALLBACK(follow_drag), sidebar);
	g_signal_connect(drag, "drag-end", G_CALLBACK(end_drag), sidebar);
	GtkWidget *overlay = gtk_overlay_new();
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(drag), GTK_PHASE_CAPTURE);
	gtk_widget_add_controller(overlay, GTK_EVENT_CONTROLLER(drag));
	gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(split));
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), sidebar->handle);
	g_object_set_data_full(G_OBJECT(overlay), "sidebar-handle", sidebar, g_free);
	g_signal_connect_swapped(split, "notify::collapsed", G_CALLBACK(update_handle), sidebar);
	if (ADW_IS_OVERLAY_SPLIT_VIEW(split))
		g_signal_connect_swapped(split, "notify::show-sidebar", G_CALLBACK(update_handle), sidebar);
	set_width(sidebar, width);
	update_handle(sidebar);
	return overlay;
}


GtkWidget *add_sidebar_handle(AdwOverlaySplitView *split, int width, int minimum, int maximum,
	SidebarResized resized, gpointer data) {
	return attach_handle(GTK_WIDGET(split), width, minimum, maximum, resized, data);
}


GtkWidget *add_navigation_sidebar_handle(AdwNavigationSplitView *split, int width, int minimum, int maximum,
	SidebarResized resized, gpointer data) {
	return attach_handle(GTK_WIDGET(split), width, minimum, maximum, resized, data);
}


GtkWidget *add_panes_sidebar_handle(CrimsonPanes *panes, int width, int minimum, int maximum, SidebarResized resized,
	gpointer data) {
	return attach_handle(GTK_WIDGET(panes), width, minimum, maximum, resized, data);
}

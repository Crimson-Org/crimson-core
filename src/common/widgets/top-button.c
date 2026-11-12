#include <adwaita.h>

#include "common/widgets/smooth-scroll.h"
#include "common/widgets/top-button.h"

/* The button shows once the view is this many screens down */
#define SCREENS_BEFORE_BUTTON 1.0
#define GLIDE_MILLISECONDS 360
/* A plain arrow, the same as the button down to the latest message in Crimson Chat */
#define TOP_ICON "go-up-symbolic"
#define SHOWN_CLASS "shown"


/* The stylesheet rises and fades the button in, and sinks it away, and a hidden button lets clicks pass through */
static void follow_scrolling(GtkAdjustment *adjustment, GtkWidget *button) {
	double page = gtk_adjustment_get_page_size(adjustment);
	gboolean is_down = page > 0 && gtk_adjustment_get_value(adjustment) > page * SCREENS_BEFORE_BUTTON;
	if (gtk_widget_has_css_class(button, SHOWN_CLASS) == is_down)
		return;
	if (is_down)
		gtk_widget_add_css_class(button, SHOWN_CLASS);
	else
		gtk_widget_remove_css_class(button, SHOWN_CLASS);
	gtk_widget_set_can_target(button, is_down);
	gtk_widget_set_focusable(button, is_down);
}


static void go_to_top(GtkButton *button, GtkScrolledWindow *scroller) {
	glide_scroller(scroller, 0, GLIDE_MILLISECONDS);
}


void glide_page_to_top(GtkWidget *page) {
	go_to_top(NULL, GTK_SCROLLED_WINDOW(gtk_overlay_get_child(GTK_OVERLAY(page))));
}


GtkWidget *add_top_button(GtkScrolledWindow *scroller, const char *tooltip) {
	GtkWidget *button = gtk_button_new_from_icon_name(TOP_ICON);
	gtk_widget_set_tooltip_text(button, tooltip);
	gtk_widget_add_css_class(button, "circular");
	gtk_widget_add_css_class(button, "osd");
	gtk_widget_add_css_class(button, "top-button");
	g_signal_connect(button, "clicked", G_CALLBACK(go_to_top), scroller);
	gtk_widget_set_halign(button, GTK_ALIGN_END);
	gtk_widget_set_valign(button, GTK_ALIGN_END);
	gtk_widget_set_can_target(button, FALSE);
	gtk_widget_set_focusable(button, FALSE);
	GtkAdjustment *adjustment = gtk_scrolled_window_get_vadjustment(scroller);
	g_signal_connect_object(adjustment, "value-changed", G_CALLBACK(follow_scrolling), button, 0);
	g_signal_connect_object(adjustment, "changed", G_CALLBACK(follow_scrolling), button, 0);
	GtkWidget *overlay = gtk_overlay_new();
	gtk_overlay_set_child(GTK_OVERLAY(overlay), GTK_WIDGET(scroller));
	gtk_overlay_add_overlay(GTK_OVERLAY(overlay), button);
	return overlay;
}

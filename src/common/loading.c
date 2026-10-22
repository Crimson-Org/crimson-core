#include <adwaita.h>

#include "loading.h"

#define SPINNER_SIZE 32
#define DELAY_SOURCE_KEY "loading-delay-source"


static gboolean show_spinner(gpointer data) {
	GtkWidget *spinner = data;
	g_object_steal_data(G_OBJECT(spinner), DELAY_SOURCE_KEY);
	gtk_widget_add_css_class(spinner, "shown");
	return G_SOURCE_REMOVE;
}


static void cancel_delay(gpointer source) {
	g_source_remove(GPOINTER_TO_UINT(source));
}


GtkWidget *create_loading_spinner(void) {
	GtkWidget *spinner = adw_spinner_new();
	gtk_widget_add_css_class(spinner, "loading-spinner");
	gtk_widget_set_size_request(spinner, SPINNER_SIZE, SPINNER_SIZE);
	gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(spinner, GTK_ALIGN_CENTER);
	/* The timer goes away with the spinner, so a page closed while loading leaves nothing behind */
	guint source = g_timeout_add(SPINNER_DELAY_MILLISECONDS, show_spinner, spinner);
	g_object_set_data_full(G_OBJECT(spinner), DELAY_SOURCE_KEY, GUINT_TO_POINTER(source), cancel_delay);
	return spinner;
}


void fade_in_widget(GtkWidget *widget) {
	gtk_widget_set_opacity(widget, 0);
	AdwAnimationTarget *target = adw_property_animation_target_new(G_OBJECT(widget), "opacity");
	AdwAnimation *fade = adw_timed_animation_new(widget, 0, 1, FADE_IN_MILLISECONDS, target);
	adw_timed_animation_set_easing(ADW_TIMED_ANIMATION(fade), ADW_EASE_OUT_CUBIC);
	adw_animation_play(fade);
	g_object_unref(fade);
}

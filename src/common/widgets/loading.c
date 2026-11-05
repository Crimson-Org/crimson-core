#include <adwaita.h>

#include "common/widgets/loading.h"

#define SPINNER_SIZE 32
#define DELAY_SOURCE_KEY "loading-delay-source"


static gboolean show_spinner(gpointer data) {
	GtkWidget *spinner = data;
	g_object_steal_data(G_OBJECT(spinner), DELAY_SOURCE_KEY);
	fade_in_widget(spinner);
	return G_SOURCE_REMOVE;
}


static void cancel_delay(gpointer source) {
	g_source_remove(GPOINTER_TO_UINT(source));
}


/* The wait starts each time the spinner is shown, so a spinner made early and shown later waits too */
static void start_delay(GtkWidget *spinner) {
	gtk_widget_set_opacity(spinner, 0);
	guint source = g_timeout_add(SPINNER_DELAY_MILLISECONDS, show_spinner, spinner);
	/* The timer goes away with the spinner, so a page closed while loading leaves nothing behind */
	g_object_set_data_full(G_OBJECT(spinner), DELAY_SOURCE_KEY, GUINT_TO_POINTER(source), cancel_delay);
}


static void stop_delay(GtkWidget *spinner) {
	g_object_set_data(G_OBJECT(spinner), DELAY_SOURCE_KEY, NULL);
	gtk_widget_set_opacity(spinner, 0);
}


GtkWidget *create_loading_spinner(void) {
	GtkWidget *spinner = adw_spinner_new();
	gtk_widget_add_css_class(spinner, "loading-spinner");
	gtk_widget_set_size_request(spinner, SPINNER_SIZE, SPINNER_SIZE);
	gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_opacity(spinner, 0);
	g_signal_connect(spinner, "map", G_CALLBACK(start_delay), NULL);
	g_signal_connect(spinner, "unmap", G_CALLBACK(stop_delay), NULL);
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

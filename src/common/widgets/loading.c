#include <adwaita.h>

#include "common/widgets/loading.h"

#define SPINNER_SIZE 32
#define DELAY_KEY "loading-delay"


/* One widget waiting to fade in, with what runs once it does */
typedef struct {
	GtkWidget *widget;
	guint source;
	GCallback shown;
	gpointer data;
} DelayedReveal;


static gboolean reveal_widget(gpointer data) {
	DelayedReveal *reveal = data;
	reveal->source = 0;
	fade_in_widget(reveal->widget);
	if (reveal->shown)
		((void (*)(gpointer))reveal->shown)(reveal->data);
	return G_SOURCE_REMOVE;
}


/* The wait starts each time the widget is shown, so a widget made early and shown later waits too */
static void start_delay(GtkWidget *widget, DelayedReveal *reveal) {
	gtk_widget_set_opacity(widget, 0);
	g_clear_handle_id(&reveal->source, g_source_remove);
	reveal->source = g_timeout_add(SPINNER_DELAY_MILLISECONDS, reveal_widget, reveal);
}


static void stop_delay(GtkWidget *widget, DelayedReveal *reveal) {
	g_clear_handle_id(&reveal->source, g_source_remove);
	gtk_widget_set_opacity(widget, 0);
}


/* The timer goes away with the widget, so a page closed while loading leaves nothing behind */
static void free_reveal(gpointer data) {
	DelayedReveal *reveal = data;
	g_clear_handle_id(&reveal->source, g_source_remove);
	g_free(reveal);
}


void reveal_after_delay(GtkWidget *widget, GCallback shown, gpointer data) {
	DelayedReveal *reveal = g_new0(DelayedReveal, 1);
	*reveal = (DelayedReveal){widget, 0, shown, data};
	gtk_widget_set_opacity(widget, 0);
	g_signal_connect(widget, "map", G_CALLBACK(start_delay), reveal);
	g_signal_connect(widget, "unmap", G_CALLBACK(stop_delay), reveal);
	g_object_set_data_full(G_OBJECT(widget), DELAY_KEY, reveal, free_reveal);
}


GtkWidget *create_loading_spinner(void) {
	GtkWidget *spinner = adw_spinner_new();
	gtk_widget_add_css_class(spinner, "loading-spinner");
	gtk_widget_set_size_request(spinner, SPINNER_SIZE, SPINNER_SIZE);
	gtk_widget_set_halign(spinner, GTK_ALIGN_CENTER);
	gtk_widget_set_valign(spinner, GTK_ALIGN_CENTER);
	reveal_after_delay(spinner, NULL, NULL);
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

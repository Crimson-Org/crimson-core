#include <gtk/gtk.h>

#include "button-presses.h"


/* A release only runs a button inside its rounded outline, while its icon or label reaches past a round button's
 * curve, so the content lets presses through and a press counts exactly where a release would */
static gboolean let_presses_through(GSignalInvocationHint *hint, guint count, const GValue *values, gpointer data) {
	GtkWidget *widget = g_value_get_object(&values[0]);
	GtkWidget *parent = gtk_widget_get_parent(widget);
	if (parent && GTK_IS_BUTTON(parent) && gtk_button_get_child(GTK_BUTTON(parent)) == widget)
		gtk_widget_set_can_target(widget, FALSE);
	return TRUE;
}


void match_button_press_areas(void) {
	g_signal_add_emission_hook(g_signal_lookup("map", GTK_TYPE_WIDGET), 0, let_presses_through, NULL, NULL);
}

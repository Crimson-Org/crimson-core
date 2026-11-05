#include "common/window/navigation.h"

/* Mouse buttons eight and nine are the side buttons that browsers use for back and forward */
#define BACK_BUTTON 8
#define FORWARD_BUTTON 9
#define BACK_ACTION_KEY "mouse-back-action"
#define FORWARD_ACTION_KEY "mouse-forward-action"


static void press_side_button(GtkGestureClick *gesture, int presses, double x, double y, GtkWidget *window) {
	guint button = gtk_gesture_single_get_current_button(GTK_GESTURE_SINGLE(gesture));
	const char *key = button == BACK_BUTTON ? BACK_ACTION_KEY : button == FORWARD_BUTTON ? FORWARD_ACTION_KEY : NULL;
	const char *action = key ? g_object_get_data(G_OBJECT(gesture), key) : NULL;
	if (!action)
		return;
	gtk_gesture_set_state(GTK_GESTURE(gesture), GTK_EVENT_SEQUENCE_CLAIMED);
	gtk_widget_activate_action(window, action, NULL);
}


void add_mouse_navigation(GtkWidget *window, const char *back_action, const char *forward_action) {
	GtkGesture *click = gtk_gesture_click_new();
	gtk_gesture_single_set_button(GTK_GESTURE_SINGLE(click), 0);
	gtk_event_controller_set_propagation_phase(GTK_EVENT_CONTROLLER(click), GTK_PHASE_CAPTURE);
	g_object_set_data_full(G_OBJECT(click), BACK_ACTION_KEY, g_strdup(back_action), g_free);
	g_object_set_data_full(G_OBJECT(click), FORWARD_ACTION_KEY, g_strdup(forward_action), g_free);
	g_signal_connect(click, "pressed", G_CALLBACK(press_side_button), window);
	gtk_widget_add_controller(window, GTK_EVENT_CONTROLLER(click));
}

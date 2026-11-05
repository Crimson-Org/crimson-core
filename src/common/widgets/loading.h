#pragma once

#include <gtk/gtk.h>

/* Work that finishes sooner than this never shows a spinner */
#define SPINNER_DELAY_MILLISECONDS 500
#define FADE_IN_MILLISECONDS 300

/* A spinner that stays invisible for the delay each time it is shown, then fades in, for content on its way */
GtkWidget *create_loading_spinner(void);
/* Any widget that waits the same way, such as a waiting sign of another shape, with shown run as it appears */
void reveal_after_delay(GtkWidget *widget, GCallback shown, gpointer data);

/* Fades a widget in from nothing, for content that has just arrived */
void fade_in_widget(GtkWidget *widget);

#pragma once

#include <gtk/gtk.h>

/* Work that finishes sooner than this never shows a spinner */
#define SPINNER_DELAY_MILLISECONDS 500
#define FADE_IN_MILLISECONDS 300

/* A spinner that stays invisible for the delay each time it is shown, then fades in, for content on its way */
GtkWidget *create_loading_spinner(void);

/* Fades a widget in from nothing, for content that has just arrived */
void fade_in_widget(GtkWidget *widget);

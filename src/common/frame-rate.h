#pragma once

#include <gtk/gtk.h>

/* Writes the window's frame rate, frame work and answers to clicks and keys to the session log after every ten
 * seconds of drawing, and each slow answer or frozen moment at once */
void watch_frame_rate(GtkWidget *window);

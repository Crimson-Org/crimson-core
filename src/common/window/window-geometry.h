#pragma once

#include <gtk/gtk.h>

/* Window size and whether it was maximized, kept between launches */
typedef struct {
	int width, height;
	gboolean is_maximized;
} WindowGeometry;

void load_window_geometry(GKeyFile *file, WindowGeometry *geometry, int default_width, int default_height);
void store_window_geometry(GKeyFile *file, const WindowGeometry *geometry);

void apply_window_geometry(GtkWindow *window, const WindowGeometry *geometry);
void read_window_geometry(GtkWindow *window, WindowGeometry *geometry);
/* Takes a closing window off the screen at once, so closing feels instant while saving and freeing still follow */
void hide_closing_window(GtkWindow *window);

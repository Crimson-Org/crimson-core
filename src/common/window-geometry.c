#include "key-store.h"
#include "window-geometry.h"

#define GEOMETRY_GROUP "window"
#define MINIMUM_WINDOW_SIZE 320
#define MAXIMUM_WINDOW_SIZE 16384


void load_window_geometry(GKeyFile *file, WindowGeometry *geometry, int default_width, int default_height) {
	geometry->width = read_key_integer(file, GEOMETRY_GROUP, "width", default_width, MINIMUM_WINDOW_SIZE,
		MAXIMUM_WINDOW_SIZE);
	geometry->height = read_key_integer(file, GEOMETRY_GROUP, "height", default_height, MINIMUM_WINDOW_SIZE,
		MAXIMUM_WINDOW_SIZE);
	geometry->is_maximized = g_key_file_get_boolean(file, GEOMETRY_GROUP, "maximized", NULL);
}


void store_window_geometry(GKeyFile *file, const WindowGeometry *geometry) {
	g_key_file_set_integer(file, GEOMETRY_GROUP, "width", geometry->width);
	g_key_file_set_integer(file, GEOMETRY_GROUP, "height", geometry->height);
	g_key_file_set_boolean(file, GEOMETRY_GROUP, "maximized", geometry->is_maximized);
}


void apply_window_geometry(GtkWindow *window, const WindowGeometry *geometry) {
	gtk_window_set_default_size(window, geometry->width, geometry->height);
	if (geometry->is_maximized)
		gtk_window_maximize(window);
}


void read_window_geometry(GtkWindow *window, WindowGeometry *geometry) {
	gtk_window_get_default_size(window, &geometry->width, &geometry->height);
	geometry->is_maximized = gtk_window_is_maximized(window);
}

#pragma once

#include <gtk/gtk.h>

/* Called once for each drop, with the files in the order they were dropped */
typedef void (*FilesDropped)(GFile **files, guint count, gpointer data);

/* Lets files from a file manager be dropped on the widget, which hands them to the function */
void add_file_drop(GtkWidget *widget, FilesDropped dropped, gpointer data);

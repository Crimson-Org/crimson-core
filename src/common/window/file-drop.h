#pragma once

#include <gtk/gtk.h>

/* Called once for each file dropped */
typedef void (*FileDropped)(GFile *file, gpointer data);

/* Lets files from a file manager be dropped on the widget, which hands each one to the function */
void add_file_drop(GtkWidget *widget, FileDropped dropped, gpointer data);

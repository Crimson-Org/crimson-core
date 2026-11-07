#pragma once

#include <gtk/gtk.h>

/* A small round "?" beside something that needs explaining, which pops up the explanation. The name is its tooltip
 * and what a screen reader says, such as "Help" */
GtkWidget *create_help_button(const char *name, const char *text);

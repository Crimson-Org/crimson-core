#pragma once

#include <gtk/gtk.h>

#include "identity.h"

/* Everything that differs between programs, so each one starts the same way */
typedef struct {
	ApplicationIdentity identity;
	/* Called once the toolkit is ready, after the shared styles and icon are in place */
	void (*start_up)(GApplication *application);
	void (*activate)(GApplication *application);
	void (*open)(GApplication *application, GFile **files, int count, const char *hint);
	/* Returns minus one to carry on, or the exit status when the options were handled without a window */
	int (*handle_options)(GApplication *application, GVariantDict *options);
	const GOptionEntry *options;
	const char *parameter_summary;
} ApplicationSetup;

int run_application(const ApplicationSetup *setup, int argument_count, char **arguments);

/* Runs the function on the widget's next frame, so the window is on screen before the slow work starts */
void run_after_frame(GtkWidget *widget, GSourceFunc function, gpointer data);

/* Puts a stylesheet from the program's resources on every window */
void load_style_resource(const char *name);

/* Writes the moment of the window's first frame to the session log, then its frame rate summaries */
void note_first_frame(GtkWidget *window);

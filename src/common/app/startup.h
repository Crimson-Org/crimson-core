#pragma once

#include <gtk/gtk.h>

#include "common/app/identity.h"

/* A shortcut that runs an action, with up to three keys, such as "<Control>q" */
#define SHORTCUT_KEYS 4
typedef struct {
	const char *action;
	const char *keys[SHORTCUT_KEYS];
} Shortcut;

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
	/* Opens the settings over a window, behind app.settings, or NULL for a program without settings */
	void (*show_settings)(GtkWidget *window);
	/* Ends the program behind app.quit, or NULL to close every window, which suits programs that end with their
	 * last window */
	void (*quit)(GApplication *application);
	/* The program's own shortcuts, which may also give app.about, app.settings or app.quit other keys */
	const Shortcut *shortcuts;
	guint shortcut_count;
} ApplicationSetup;

int run_application(const ApplicationSetup *setup, int argument_count, char **arguments);

/* Runs the function on the widget's next frame, so the window is on screen before the slow work starts, then frees
 * the data with destroy, which also runs when the widget goes away before its frame */
void run_after_frame(GtkWidget *widget, GSourceFunc function, gpointer data, GDestroyNotify destroy);

/* Puts a stylesheet from the program's resources on every window */
void load_style_resource(const char *name);

/* Writes when the program's first window was made, shown and first drawn to the session log, then the frame rate
 * summaries of every window watched */
void watch_window_frames(GtkWidget *window);

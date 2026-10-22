#pragma once

#include <gtk/gtk.h>

/* The panel icon, which shows or hides the window on a click and offers the program's own menu on a right click */
typedef struct Tray Tray;

/* One menu entry running a window or application action, or a separator when it has no label */
typedef struct {
	const char *label;
	const char *action;
} TrayEntry;

typedef struct {
	const TrayEntry *entries;
	guint entry_count;
	/* Run by a middle click on the icon, or NULL */
	const char *secondary_action;
	/* Given one step per wheel notch, positive or negative, or NULL */
	const char *scroll_action;
} TraySetup;

Tray *create_tray(GApplication *application, GtkWindow *window, const TraySetup *setup);
/* Changes the label of one menu entry, such as Play becoming Pause */
void set_tray_label(Tray *tray, guint index, const char *label);
/* Whether a panel took the icon, since without one a hidden window could not be brought back */
gboolean is_tray_shown(Tray *tray);

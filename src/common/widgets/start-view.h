#pragma once

#include <gtk/gtk.h>

/* The page a Crimson app shows with nothing open: its icon and name, a row of cards for the ways to start, a hint, and
 * the things opened lately */

/* One way to start, such as Open, as a card with its icon, its name and the shortcut the app gives its action */
typedef struct {
	const char *icon, *title, *action;
} StartChoice;

/* One thing opened lately: its icon, its name and the folder it sits in, the tooltip, and the key handed back when it
 * is opened or removed, such as its path */
typedef struct {
	GIcon *icon;
	const char *name, *folder, *tooltip, *key;
} StartRecent;

typedef void (*StartRecentChosen)(const char *key, gpointer data);

typedef struct {
	const StartChoice *choices;
	guint choice_count;
	/* A line under the cards, or NULL */
	const char *hint;
	/* The Recent card's heading and its remove button's name */
	const char *recent_heading, *remove_text;
	StartRecentChosen open, forget;
	gpointer data;
} StartViewSetup;

/* The setup is kept, not copied, so it must outlive the view, such as a static one */
GtkWidget *create_start_view(const StartViewSetup *setup);
/* Shows these items in the Recent card, which hides while there are none */
void set_start_view_recent(GtkWidget *view, const StartRecent *items, guint count);
/* The Recent card, such as to dim it while something opens */
GtkWidget *get_start_view_recent(GtkWidget *view);
/* The card of the choice at this place, such as the one to give the keyboard while the page shows */
GtkWidget *get_start_view_choice(GtkWidget *view, guint index);

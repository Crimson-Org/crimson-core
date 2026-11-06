#include "common/app/identity.h"
#include "common/widgets/spacing.h"
#include "common/widgets/start-view.h"

#define START_ICON_SIZE 96
#define CHOICE_ICON_SIZE 32
/* The Recent card is as wide as two cards, and long folders shorten from their start */
#define RECENT_WIDTH 480
#define FOLDER_CHARACTERS 40
#define SETUP_KEY "start-setup"
#define LIST_KEY "start-list"
#define RECENT_KEY "start-recent"
#define CHOICES_KEY "start-choices"
#define ITEM_KEY "start-key"
#define REMOVE_ICON "window-close-symbolic"


static GtkWidget *create_choice(const StartChoice *choice) {
	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING_SMALL);
	GtkWidget *image = gtk_image_new_from_icon_name(choice->icon);
	gtk_image_set_pixel_size(GTK_IMAGE(image), CHOICE_ICON_SIZE);
	gtk_widget_set_margin_bottom(image, SPACING_TIGHT);
	gtk_box_append(GTK_BOX(content), image);
	gtk_box_append(GTK_BOX(content), gtk_label_new(choice->title));
	GtkApplication *application = GTK_APPLICATION(g_application_get_default());
	g_auto(GStrv) accelerators = gtk_application_get_accels_for_action(application, choice->action);
	guint key = 0;
	GdkModifierType modifiers = 0;
	if (accelerators[0] && gtk_accelerator_parse(accelerators[0], &key, &modifiers)) {
		g_autofree char *shortcut = gtk_accelerator_get_label(key, modifiers);
		GtkWidget *hint = gtk_label_new(shortcut);
		gtk_widget_add_css_class(hint, "start-shortcut");
		gtk_box_append(GTK_BOX(content), hint);
	}
	GtkWidget *button = gtk_button_new();
	gtk_widget_add_css_class(button, "card");
	gtk_widget_add_css_class(button, "start-button");
	gtk_button_set_child(GTK_BUTTON(button), content);
	gtk_actionable_set_action_name(GTK_ACTIONABLE(button), choice->action);
	return button;
}


/* A recent item's remove button shows while the pointer is over its row or the keyboard is on the button */
static void show_remove(GtkWidget *remove, gboolean is_shown) {
	if (is_shown || !gtk_widget_has_focus(remove))
		gtk_widget_set_opacity(remove, is_shown ? 1 : 0);
}


static void enter_row(GtkEventControllerMotion *motion, double x, double y, GtkWidget *remove) {
	show_remove(remove, TRUE);
}


static void leave_row(GtkEventControllerMotion *motion, GtkWidget *remove) {
	show_remove(remove, FALSE);
}


static void focus_remove(GtkEventControllerFocus *focus, GtkWidget *remove) {
	gtk_widget_set_opacity(remove, 1);
}


static void blur_remove(GtkEventControllerFocus *focus, GtkWidget *remove) {
	gtk_widget_set_opacity(remove, 0);
}


static void press_item(GtkButton *button, const StartViewSetup *setup) {
	if (setup->open)
		setup->open(g_object_get_data(G_OBJECT(button), ITEM_KEY), setup->data);
}


/* The row goes at once, the keyboard moves on to the next row, or the one before when it was the last, and the card
 * hides once it is empty, so the app only has to forget the item */
static void press_remove(GtkButton *button, const StartViewSetup *setup) {
	GtkWidget *row = gtk_widget_get_parent(GTK_WIDGET(button));
	GtkWidget *list = gtk_widget_get_parent(row);
	GtkWidget *next = gtk_widget_get_next_sibling(row);
	if (!next)
		next = gtk_widget_get_prev_sibling(row);
	g_autofree char *key = g_strdup(g_object_get_data(G_OBJECT(button), ITEM_KEY));
	gboolean had_focus = gtk_widget_has_focus(GTK_WIDGET(button));
	gtk_box_remove(GTK_BOX(list), row);
	if (next && had_focus)
		gtk_widget_grab_focus(gtk_widget_get_last_child(next));
	if (!next)
		gtk_widget_set_visible(gtk_widget_get_parent(list), FALSE);
	if (setup->forget)
		setup->forget(key, setup->data);
}


static GtkWidget *create_recent_row(const StartRecent *item, const StartViewSetup *setup) {
	GtkWidget *content = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_NORMAL);
	GtkWidget *image = gtk_image_new_from_gicon(item->icon);
	gtk_image_set_icon_size(GTK_IMAGE(image), GTK_ICON_SIZE_LARGE);
	gtk_box_append(GTK_BOX(content), image);
	GtkWidget *name = gtk_label_new(item->name);
	gtk_label_set_ellipsize(GTK_LABEL(name), PANGO_ELLIPSIZE_MIDDLE);
	gtk_label_set_xalign(GTK_LABEL(name), 0);
	gtk_widget_set_hexpand(name, TRUE);
	gtk_box_append(GTK_BOX(content), name);
	/* Folders form one column that starts at the same place on every row */
	GtkWidget *folder = gtk_label_new(item->folder);
	gtk_label_set_ellipsize(GTK_LABEL(folder), PANGO_ELLIPSIZE_START);
	gtk_label_set_width_chars(GTK_LABEL(folder), FOLDER_CHARACTERS);
	gtk_label_set_max_width_chars(GTK_LABEL(folder), FOLDER_CHARACTERS);
	gtk_label_set_xalign(GTK_LABEL(folder), 0);
	gtk_widget_add_css_class(folder, "dim-label");
	gtk_box_append(GTK_BOX(content), folder);
	GtkWidget *button = gtk_button_new();
	gtk_widget_add_css_class(button, "flat");
	gtk_widget_set_hexpand(button, TRUE);
	gtk_button_set_child(GTK_BUTTON(button), content);
	gtk_widget_set_tooltip_text(button, item->tooltip);
	g_object_set_data_full(G_OBJECT(button), ITEM_KEY, g_strdup(item->key), g_free);
	g_signal_connect(button, "clicked", G_CALLBACK(press_item), (gpointer)setup);
	GtkWidget *remove = gtk_button_new_from_icon_name(REMOVE_ICON);
	gtk_widget_add_css_class(remove, "flat");
	gtk_widget_add_css_class(remove, "circular");
	gtk_widget_set_valign(remove, GTK_ALIGN_CENTER);
	gtk_widget_set_tooltip_text(remove, setup->remove_text);
	gtk_accessible_update_property(GTK_ACCESSIBLE(remove), GTK_ACCESSIBLE_PROPERTY_LABEL, setup->remove_text, -1);
	gtk_widget_set_opacity(remove, 0);
	g_object_set_data_full(G_OBJECT(remove), ITEM_KEY, g_strdup(item->key), g_free);
	g_signal_connect(remove, "clicked", G_CALLBACK(press_remove), (gpointer)setup);
	GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_HAIRLINE);
	gtk_widget_add_css_class(row, "recent-row");
	gtk_box_append(GTK_BOX(row), button);
	gtk_box_append(GTK_BOX(row), remove);
	GtkEventController *motion = gtk_event_controller_motion_new();
	g_signal_connect(motion, "enter", G_CALLBACK(enter_row), remove);
	g_signal_connect(motion, "leave", G_CALLBACK(leave_row), remove);
	gtk_widget_add_controller(row, motion);
	GtkEventController *focus = gtk_event_controller_focus_new();
	g_signal_connect(focus, "enter", G_CALLBACK(focus_remove), remove);
	g_signal_connect(focus, "leave", G_CALLBACK(blur_remove), remove);
	gtk_widget_add_controller(remove, focus);
	return row;
}


void set_start_view_recent(GtkWidget *view, const StartRecent *items, guint count) {
	const StartViewSetup *setup = g_object_get_data(G_OBJECT(view), SETUP_KEY);
	GtkWidget *list = g_object_get_data(G_OBJECT(view), LIST_KEY), *child;
	while ((child = gtk_widget_get_first_child(list)))
		gtk_box_remove(GTK_BOX(list), child);
	for (guint index = 0; index < count; index++)
		gtk_box_append(GTK_BOX(list), create_recent_row(&items[index], setup));
	gtk_widget_set_visible(g_object_get_data(G_OBJECT(view), RECENT_KEY), count > 0);
}


static GtkWidget *create_recent(const StartViewSetup *setup, GtkWidget *view) {
	GtkWidget *list = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
	gtk_widget_add_css_class(list, "card");
	gtk_widget_add_css_class(list, "recent-list");
	GtkWidget *heading = gtk_label_new(setup->recent_heading);
	gtk_widget_add_css_class(heading, "heading");
	gtk_label_set_xalign(GTK_LABEL(heading), 0);
	GtkWidget *recent = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING_SMALL);
	gtk_widget_add_css_class(recent, "recent-files");
	gtk_widget_set_size_request(recent, RECENT_WIDTH, -1);
	gtk_widget_set_visible(recent, FALSE);
	gtk_box_append(GTK_BOX(recent), heading);
	gtk_box_append(GTK_BOX(recent), list);
	g_object_set_data(G_OBJECT(view), LIST_KEY, list);
	g_object_set_data(G_OBJECT(view), RECENT_KEY, recent);
	return recent;
}


GtkWidget *create_start_view(const StartViewSetup *given) {
	/* The view keeps its own copy of the setup, whose texts and choices are the app's constants */
	StartViewSetup *setup = g_memdup2(given, sizeof(StartViewSetup));
	GtkWidget *column = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING_LARGE);
	gtk_widget_add_css_class(column, "start-view");
	gtk_widget_set_valign(column, GTK_ALIGN_CENTER);
	gtk_widget_set_halign(column, GTK_ALIGN_CENTER);
	/* A short window scrolls the page rather than growing to fit every recent item */
	GtkWidget *view = gtk_scrolled_window_new();
	gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(view), GTK_POLICY_NEVER, GTK_POLICY_AUTOMATIC);
	gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(view), column);
	gtk_widget_set_hexpand(view, TRUE);
	gtk_widget_set_vexpand(view, TRUE);
	g_object_set_data_full(G_OBJECT(view), SETUP_KEY, setup, g_free);
	GtkWidget *heading = gtk_box_new(GTK_ORIENTATION_VERTICAL, SPACING_NORMAL);
	GtkWidget *icon = gtk_image_new_from_icon_name(get_application_identity()->id);
	gtk_image_set_pixel_size(GTK_IMAGE(icon), START_ICON_SIZE);
	gtk_widget_add_css_class(icon, "icon-dropshadow");
	gtk_box_append(GTK_BOX(heading), icon);
	GtkWidget *title = gtk_label_new(get_application_identity()->name);
	gtk_widget_add_css_class(title, "title-1");
	gtk_box_append(GTK_BOX(heading), title);
	gtk_box_append(GTK_BOX(column), heading);
	/* Every card shares one size whatever its label */
	GtkWidget *choices = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, SPACING_ROOMY);
	gtk_box_set_homogeneous(GTK_BOX(choices), TRUE);
	gtk_widget_set_halign(choices, GTK_ALIGN_CENTER);
	for (guint index = 0; index < setup->choice_count; index++)
		gtk_box_append(GTK_BOX(choices), create_choice(&setup->choices[index]));
	gtk_box_append(GTK_BOX(column), choices);
	g_object_set_data(G_OBJECT(view), CHOICES_KEY, choices);
	if (setup->hint) {
		GtkWidget *hint = gtk_label_new(setup->hint);
		gtk_widget_add_css_class(hint, "dim-label");
		gtk_box_append(GTK_BOX(column), hint);
	}
	gtk_box_append(GTK_BOX(column), create_recent(setup, view));
	return view;
}


GtkWidget *get_start_view_recent(GtkWidget *view) {
	return g_object_get_data(G_OBJECT(view), RECENT_KEY);
}


GtkWidget *get_start_view_choice(GtkWidget *view, guint index) {
	GtkWidget *choice = gtk_widget_get_first_child(g_object_get_data(G_OBJECT(view), CHOICES_KEY));
	for (; choice && index > 0; index--)
		choice = gtk_widget_get_next_sibling(choice);
	return choice;
}

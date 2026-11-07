#include "common/widgets/help-button.h"

#define HELP_ICON "dialog-question-symbolic"
/* An explanation wraps at this many characters, so it reads as a short paragraph */
#define HELP_WIDTH_CHARS 34


GtkWidget *create_help_button(const char *name, const char *text) {
	GtkWidget *label = gtk_label_new(text);
	gtk_label_set_wrap(GTK_LABEL(label), TRUE);
	gtk_label_set_max_width_chars(GTK_LABEL(label), HELP_WIDTH_CHARS);
	gtk_label_set_xalign(GTK_LABEL(label), 0);
	gtk_widget_add_css_class(label, "help-text");
	GtkWidget *popover = gtk_popover_new();
	gtk_popover_set_child(GTK_POPOVER(popover), label);
	GtkWidget *button = gtk_menu_button_new();
	gtk_menu_button_set_icon_name(GTK_MENU_BUTTON(button), HELP_ICON);
	gtk_menu_button_set_popover(GTK_MENU_BUTTON(button), popover);
	gtk_widget_set_tooltip_text(button, name);
	gtk_accessible_update_property(GTK_ACCESSIBLE(button), GTK_ACCESSIBLE_PROPERTY_LABEL, name, -1);
	gtk_widget_add_css_class(button, "help-button");
	gtk_widget_set_valign(button, GTK_ALIGN_CENTER);
	return button;
}

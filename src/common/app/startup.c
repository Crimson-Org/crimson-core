#include <adwaita.h>

#include "common/app/startup.h"
#include "common/widgets/button-presses.h"
#include "common/app/frame-rate.h"
#include "common/app/log.h"
#include "common/widgets/dialogs.h"
#include "common/widgets/spacing.h"

#define COMMON_STYLE "common.css"

static const ApplicationSetup *current_setup;


typedef struct {
	GSourceFunc function;
	gpointer data;
	GDestroyNotify destroy;
} FrameRequest;


static gboolean run_frame_request(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
	FrameRequest *request = data;
	request->function(request->data);
	return G_SOURCE_REMOVE;
}


/* Runs after the request ran, or when the widget went away before its frame */
static void free_frame_request(gpointer data) {
	FrameRequest *request = data;
	if (request->destroy)
		request->destroy(request->data);
	g_free(request);
}


void run_after_frame(GtkWidget *widget, GSourceFunc function, gpointer data, GDestroyNotify destroy) {
	FrameRequest *request = g_new0(FrameRequest, 1);
	*request = (FrameRequest){function, data, destroy};
	gtk_widget_add_tick_callback(widget, run_frame_request, request, free_frame_request);
}


static void add_style_resource(const char *path) {
	g_autoptr(GtkCssProvider) style = gtk_css_provider_new();
	gtk_css_provider_load_from_resource(style, path);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(style),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}


void load_style_resource(const char *name) {
	g_autofree char *path = get_resource_path(name);
	add_style_resource(path);
}


#define SPACING_VARIABLE(name, value) "--spacing-" name ": " G_STRINGIFY(value) "px;"

/* The spacing scale reaches the stylesheets as named values, such as var(--spacing-normal), so a gap in CSS and the
 * same gap in code always come from one number */
static const char SPACING_STYLE[] = ":root {"
	SPACING_VARIABLE("hairline", SPACING_HAIRLINE) SPACING_VARIABLE("fine", SPACING_FINE)
	SPACING_VARIABLE("tight", SPACING_TIGHT) SPACING_VARIABLE("small", SPACING_SMALL)
	SPACING_VARIABLE("snug", SPACING_SNUG) SPACING_VARIABLE("compact", SPACING_COMPACT)
	SPACING_VARIABLE("normal", SPACING_NORMAL) SPACING_VARIABLE("roomy", SPACING_ROOMY)
	SPACING_VARIABLE("wide", SPACING_WIDE) SPACING_VARIABLE("large", SPACING_LARGE)
	SPACING_VARIABLE("huge", SPACING_HUGE) "}";


static void load_core_style(void) {
	g_autoptr(GtkCssProvider) spacing = gtk_css_provider_new();
	gtk_css_provider_load_from_string(spacing, SPACING_STYLE);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(spacing),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
	add_style_resource(CORE_RESOURCES "/" COMMON_STYLE);
}


static GtkWidget *get_active_window(GApplication *application) {
	return GTK_WIDGET(gtk_application_get_active_window(GTK_APPLICATION(application)));
}


static void show_about(GSimpleAction *action, GVariant *parameter, gpointer application) {
	const ApplicationIdentity *identity = get_application_identity();
	AdwDialog *dialog = adw_about_dialog_new();
	adw_about_dialog_set_application_name(ADW_ABOUT_DIALOG(dialog), identity->name);
	adw_about_dialog_set_application_icon(ADW_ABOUT_DIALOG(dialog), identity->id);
	adw_about_dialog_set_version(ADW_ABOUT_DIALOG(dialog), identity->version);
	if (current_setup->closes_dialogs_outside)
		close_on_outside_click(dialog);
	adw_dialog_present(dialog, get_active_window(application));
}


static void open_settings(GSimpleAction *action, GVariant *parameter, gpointer application) {
	GtkWidget *window = get_active_window(application);
	if (window)
		current_setup->show_settings(window);
}


static void quit(GSimpleAction *action, GVariant *parameter, gpointer application) {
	if (current_setup->quit) {
		current_setup->quit(application);
		return;
	}
	/* Each window asks about its own unsaved work, and closing one takes it off the application's list, so the
	 * list is copied first */
	GList *windows = g_list_copy(gtk_application_get_windows(GTK_APPLICATION(application)));
	for (GList *item = windows; item; item = item->next)
		gtk_window_close(item->data);
	g_list_free(windows);
}


static void add_shortcuts(GtkApplication *application, const Shortcut *shortcuts, guint count) {
	for (guint index = 0; index < count; index++)
		gtk_application_set_accels_for_action(application, shortcuts[index].action, shortcuts[index].keys);
}


/* Every program has About, Settings when it has any, and Quit, with the same keys unless it gives others */
static void add_shell_actions(GApplication *application) {
	static const GActionEntry ENTRIES[] = {
		{"about", show_about, NULL, NULL, NULL, {0}},
		{"quit", quit, NULL, NULL, NULL, {0}},
		{"settings", open_settings, NULL, NULL, NULL, {0}},
	};
	static const Shortcut SHORTCUTS[] = {
		{"app.quit", {"<Control>q", NULL}},
		{"app.settings", {"<Control>comma", NULL}},
	};
	guint count = current_setup->show_settings ? G_N_ELEMENTS(ENTRIES) : G_N_ELEMENTS(ENTRIES) - 1;
	g_action_map_add_action_entries(G_ACTION_MAP(application), ENTRIES, count, application);
	add_shortcuts(GTK_APPLICATION(application), SHORTCUTS, current_setup->show_settings ? G_N_ELEMENTS(SHORTCUTS)
		: G_N_ELEMENTS(SHORTCUTS) - 1);
	add_shortcuts(GTK_APPLICATION(application), current_setup->shortcuts, current_setup->shortcut_count);
}


static void start_up(GApplication *application) {
	note_startup_moment("Toolkit opened");
	gtk_window_set_default_icon_name(current_setup->identity.id);
	/* AdwApplication loads the program's own style.css from its resources, so only the shared one is added here */
	load_core_style();
	match_button_press_areas();
	add_shell_actions(application);
	if (current_setup->start_up)
		current_setup->start_up(application);
	note_startup_moment("Toolkit started");
}


static int handle_local_options(GApplication *application, GVariantDict *options, gpointer data) {
	if (g_variant_dict_contains(options, "version")) {
		g_print("%s %s\n", current_setup->identity.name, current_setup->identity.version);
		return EXIT_SUCCESS;
	}
	if (current_setup->handle_options)
		return current_setup->handle_options(application, options);
	return -1;
}


int run_application(const ApplicationSetup *setup, int argument_count, char **arguments) {
	current_setup = setup;
	set_application_identity(&setup->identity);
	start_session_log(setup->identity.slug);
	note_startup_moment("Program started");
	g_autoptr(AdwApplication) application = adw_application_new(setup->identity.id, G_APPLICATION_HANDLES_OPEN);
	g_signal_connect(application, "startup", G_CALLBACK(start_up), NULL);
	g_signal_connect(application, "activate", G_CALLBACK(setup->activate), NULL);
	g_signal_connect(application, "open", G_CALLBACK(setup->open), NULL);
	g_signal_connect(application, "handle-local-options", G_CALLBACK(handle_local_options), NULL);
	const GOptionEntry version_option[] = {
		{"version", 'v', 0, G_OPTION_ARG_NONE, NULL, "Print the version", NULL},
		G_OPTION_ENTRY_NULL,
	};
	g_application_add_main_option_entries(G_APPLICATION(application), version_option);
	if (setup->options)
		g_application_add_main_option_entries(G_APPLICATION(application), setup->options);
	if (setup->parameter_summary)
		g_application_set_option_context_parameter_string(G_APPLICATION(application), setup->parameter_summary);
	int status = g_application_run(G_APPLICATION(application), argument_count, arguments);
	stop_session_log();
	return status;
}


static gboolean has_noted_startup;


static gboolean note_frame_moment(gpointer window) {
	if (!has_noted_startup)
		note_startup_moment("First frame");
	has_noted_startup = TRUE;
	watch_frame_rate(window);
	return G_SOURCE_REMOVE;
}


/* A window can be shown again, such as from the tray, which is not part of starting up, so it is watched once */
static void note_window_shown(GtkWidget *window, gpointer mark) {
	if (!has_noted_startup)
		note_startup_moment("Window shown");
	run_after_frame(window, note_frame_moment, window, NULL);
	g_signal_handlers_disconnect_by_data(window, mark);
}


void watch_window_frames(GtkWidget *window) {
	static const char WATCH_MARK[] = "watching-window-frames";
	if (!has_noted_startup)
		note_startup_moment("Window created");
	g_signal_connect(window, "map", G_CALLBACK(note_window_shown), (gpointer)WATCH_MARK);
}

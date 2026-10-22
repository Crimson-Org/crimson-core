#include <adwaita.h>

#include "startup.h"
#include "frame-rate.h"
#include "log.h"

#define COMMON_STYLE "common.css"
#define APPLICATION_STYLE "style.css"

static const ApplicationSetup *current_setup;


typedef struct {
	GSourceFunc function;
	gpointer data;
} FrameRequest;


static gboolean run_frame_request(GtkWidget *widget, GdkFrameClock *clock, gpointer data) {
	FrameRequest *request = data;
	request->function(request->data);
	g_free(request);
	return G_SOURCE_REMOVE;
}


void run_after_frame(GtkWidget *widget, GSourceFunc function, gpointer data) {
	FrameRequest *request = g_new0(FrameRequest, 1);
	*request = (FrameRequest){function, data};
	gtk_widget_add_tick_callback(widget, run_frame_request, request, NULL);
}


static gboolean has_style_resource(const char *name) {
	g_autofree char *path = get_resource_path(name);
	return g_resources_get_info(path, G_RESOURCE_LOOKUP_FLAGS_NONE, NULL, NULL, NULL);
}


void load_style_resource(const char *name) {
	g_autofree char *path = get_resource_path(name);
	g_autoptr(GtkCssProvider) style = gtk_css_provider_new();
	gtk_css_provider_load_from_resource(style, path);
	gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(style),
		GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
}


static void start_up(GApplication *application) {
	note_startup_moment("Toolkit opened");
	gtk_window_set_default_icon_name(current_setup->identity.id);
	load_style_resource(COMMON_STYLE);
	if (has_style_resource(APPLICATION_STYLE))
		load_style_resource(APPLICATION_STYLE);
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


static gboolean note_frame_moment(gpointer window) {
	note_startup_moment("First frame");
	watch_frame_rate(window);
	return G_SOURCE_REMOVE;
}


void note_first_frame(GtkWidget *window) {
	run_after_frame(window, note_frame_moment, window);
}

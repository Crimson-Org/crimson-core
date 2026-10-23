#include <adwaita.h>

#include "themes.h"

/* GtkSourceView keeps its own themes among its resources, which only exist in programs that load it */
#define SOURCE_VIEW_THEMES_RESOURCE "/org/gnome/gtksourceview/styles"
#define THEME_EXTENSION ".xml"
/* Stops parent themes and borrowed styles that point at each other from looping */
#define MAXIMUM_LOOKUP_DEPTH 8

/* How far derived window colours move from the text background towards black in dark themes or white in light */
#define SIDEBAR_SHADE 0.12
#define BAR_SHADE 0.45
#define LIFT_SHADE 0.06
#define DARK_CARD_SHADE 0.05
#define LIGHT_CARD_SHADE 0.6
/* Above this luminance an accent takes dark text instead of white */
#define BRIGHT_ACCENT 0.5

/* One style as the theme file writes it, with colours that may still name the palette */
typedef struct {
	char *foreground, *background, *use_style;
} ThemeStyle;

/* A theme with everything its file holds besides the id and name */
typedef struct {
	Theme theme;
	char *parent_id;
	/* Colour names to values, and style names to ThemeStyle */
	GHashTable *palette, *styles;
} ThemeFile;

/* A function to tell after each change */
typedef struct {
	ThemeChangedFunction on_changed;
	gpointer data;
} ThemeWatcher;

static GPtrArray *themes;
static GArray *watchers;
static GFileMonitor *folder_monitor;
static GtkCssProvider *theme_style;
static char *light_theme, *dark_theme;
static ThemeParts coloured_parts;
static GdkRGBA text_background, text_foreground;
static gboolean has_text_colours;

static void apply_themes(gboolean has_read_files);


static void free_style(gpointer data) {
	ThemeStyle *style = data;
	g_free(style->foreground);
	g_free(style->background);
	g_free(style->use_style);
	g_free(style);
}


static void free_theme_file(gpointer data) {
	ThemeFile *file = data;
	g_free(file->theme.id);
	g_free(file->theme.name);
	g_free(file->parent_id);
	g_hash_table_unref(file->palette);
	g_hash_table_unref(file->styles);
	g_free(file);
}


static const char *find_attribute(const char **names, const char **values, const char *wanted) {
	for (int index = 0; names[index]; index++)
		if (g_strcmp0(names[index], wanted) == 0)
			return values[index];
	return NULL;
}


static void read_element(GMarkupParseContext *context, const char *element, const char **names,
	const char **values, gpointer data, GError **error) {
	ThemeFile *file = data;
	if (g_strcmp0(element, "style-scheme") == 0) {
		file->theme.id = g_strdup(find_attribute(names, values, "id"));
		const char *name = find_attribute(names, values, "name");
		file->theme.name = g_strdup(name ? name : find_attribute(names, values, "_name"));
		file->parent_id = g_strdup(find_attribute(names, values, "parent-scheme"));
		return;
	}
	const char *name = find_attribute(names, values, "name");
	if (!name)
		return;
	if (g_strcmp0(element, "color") == 0) {
		g_hash_table_replace(file->palette, g_strdup(name), g_strdup(find_attribute(names, values, "value")));
		return;
	}
	if (g_strcmp0(element, "style") != 0)
		return;
	ThemeStyle *style = g_new0(ThemeStyle, 1);
	style->foreground = g_strdup(find_attribute(names, values, "foreground"));
	style->background = g_strdup(find_attribute(names, values, "background"));
	style->use_style = g_strdup(find_attribute(names, values, "use-style"));
	g_hash_table_replace(file->styles, g_strdup(name), style);
}


static ThemeFile *read_theme_file(GFile *source) {
	g_autofree char *text = NULL;
	gsize length = 0;
	if (!g_file_load_contents(source, NULL, &text, &length, NULL, NULL))
		return NULL;
	ThemeFile *file = g_new0(ThemeFile, 1);
	file->palette = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	file->styles = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, free_style);
	static const GMarkupParser PARSER = {.start_element = read_element};
	g_autoptr(GMarkupParseContext) context = g_markup_parse_context_new(&PARSER, 0, file, NULL);
	if (!g_markup_parse_context_parse(context, text, (gssize)length, NULL) || !file->theme.id) {
		free_theme_file(file);
		return NULL;
	}
	if (!file->theme.name)
		file->theme.name = g_strdup(file->theme.id);
	return file;
}


static ThemeFile *find_theme_file(const char *id) {
	for (guint index = 0; id && themes && index < themes->len; index++) {
		ThemeFile *file = g_ptr_array_index(themes, index);
		if (g_strcmp0(file->theme.id, id) == 0)
			return file;
	}
	return NULL;
}


/* A folder read earlier is kept when two themes share an id, so the user's own copy replaces a bundled one */
static void read_theme_folder(GFile *folder) {
	g_autoptr(GFileEnumerator) children = g_file_enumerate_children(folder, G_FILE_ATTRIBUTE_STANDARD_NAME,
		G_FILE_QUERY_INFO_NONE, NULL, NULL);
	GFileInfo *info = NULL;
	GFile *child = NULL;
	while (children && g_file_enumerator_iterate(children, &info, &child, NULL, NULL) && info) {
		if (!g_str_has_suffix(g_file_info_get_name(info), THEME_EXTENSION))
			continue;
		ThemeFile *file = read_theme_file(child);
		if (file && find_theme_file(file->theme.id))
			free_theme_file(file);
		else if (file)
			g_ptr_array_add(themes, file);
	}
}


static void read_theme_path(const char *path) {
	g_autoptr(GFile) folder = g_file_new_for_path(path);
	read_theme_folder(folder);
}


static void read_theme_resource(const char *path) {
	g_autofree char *address = g_strconcat("resource://", path, NULL);
	g_autoptr(GFile) folder = g_file_new_for_uri(address);
	read_theme_folder(folder);
}


static int compare_names(gconstpointer first, gconstpointer second) {
	const Theme *first_theme = *(const Theme *const *)first, *second_theme = *(const Theme *const *)second;
	return g_utf8_collate(first_theme->name, second_theme->name);
}


static void on_theme_folder_changed(GFileMonitor *monitor, GFile *file, GFile *other, GFileMonitorEvent event,
	gpointer data) {
	if (event == G_FILE_MONITOR_EVENT_CHANGES_DONE_HINT || event == G_FILE_MONITOR_EVENT_DELETED
		|| event == G_FILE_MONITOR_EVENT_RENAMED || event == G_FILE_MONITOR_EVENT_MOVED_IN)
		reload_themes();
}


/* The folder only exists once someone made a theme, so watching starts on the first read that finds it */
static void watch_theme_folder(const char *path) {
	if (folder_monitor || !g_file_test(path, G_FILE_TEST_IS_DIR))
		return;
	g_autoptr(GFile) folder = g_file_new_for_path(path);
	folder_monitor = g_file_monitor_directory(folder, G_FILE_MONITOR_WATCH_MOVES, NULL, NULL);
	if (folder_monitor)
		g_signal_connect(folder_monitor, "changed", G_CALLBACK(on_theme_folder_changed), NULL);
}


static void read_themes(void) {
	g_clear_pointer(&themes, g_ptr_array_unref);
	themes = g_ptr_array_new_with_free_func(free_theme_file);
	g_autofree char *user_folder = get_user_theme_folder();
	read_theme_path(user_folder);
	for (const char *const *data_folder = g_get_system_data_dirs(); *data_folder; data_folder++) {
		g_autofree char *path = g_build_filename(*data_folder, "gtksourceview-5", "styles", NULL);
		read_theme_path(path);
	}
	read_theme_resource(BUNDLED_THEMES_RESOURCE);
	read_theme_resource(SOURCE_VIEW_THEMES_RESOURCE);
	g_ptr_array_sort(themes, compare_names);
	watch_theme_folder(user_folder);
}


char *get_user_theme_folder(void) {
	return g_build_filename(g_get_user_data_dir(), "gtksourceview-5", "styles", NULL);
}


GPtrArray *list_themes(void) {
	if (!themes)
		read_themes();
	return themes;
}


const Theme *find_theme(const char *id) {
	ThemeFile *file = find_theme_file(id);
	return file ? &file->theme : NULL;
}


void reload_themes(void) {
	read_themes();
	if (light_theme)
		apply_themes(TRUE);
}


/* ==================================== Colours ==================================== */


static const char *find_palette_colour(const ThemeFile *file, const char *name, int depth) {
	if (!file || depth > MAXIMUM_LOOKUP_DEPTH)
		return NULL;
	const char *value = g_hash_table_lookup(file->palette, name);
	return value ? value : find_palette_colour(find_theme_file(file->parent_id), name, depth + 1);
}


/* Reads one colour of a theme style, which is only there when the theme or its parent sets it */
static gboolean get_style_colour(const ThemeFile *file, const char *name, gboolean is_background,
	GdkRGBA *colour, int depth) {
	if (!file || depth > MAXIMUM_LOOKUP_DEPTH)
		return FALSE;
	const ThemeStyle *style = g_hash_table_lookup(file->styles, name);
	if (!style)
		return get_style_colour(find_theme_file(file->parent_id), name, is_background, colour, depth + 1);
	if (style->use_style)
		return get_style_colour(file, style->use_style, is_background, colour, depth + 1);
	const char *value = is_background ? style->background : style->foreground;
	if (!value)
		return FALSE;
	const char *named = find_palette_colour(file, value, 0);
	return gdk_rgba_parse(colour, named ? named : value);
}


static GdkRGBA shade(GdkRGBA colour, double amount, gboolean is_dark) {
	float target = is_dark ? 0.0f : 1.0f;
	colour.red += (target - colour.red) * (float)amount;
	colour.green += (target - colour.green) * (float)amount;
	colour.blue += (target - colour.blue) * (float)amount;
	return colour;
}


/* A window part takes the theme's own crimson style when it has one, or a shade of the text background
 * It is left alone when the theme gives no text colour to go with it */
static gboolean get_part_colours(const ThemeFile *file, const char *style, double amount, gboolean is_dark,
	GdkRGBA *background, GdkRGBA *foreground) {
	GdkRGBA plain_background = {is_dark ? 0.12f : 1, is_dark ? 0.12f : 1, is_dark ? 0.12f : 1, 1};
	GdkRGBA plain_foreground;
	get_style_colour(file, "text", TRUE, &plain_background, 0);
	gboolean has_foreground = get_style_colour(file, "text", FALSE, &plain_foreground, 0);
	if (!get_style_colour(file, style, TRUE, background, 0))
		*background = shade(plain_background, amount, is_dark);
	if (get_style_colour(file, style, FALSE, foreground, 0))
		return TRUE;
	*foreground = plain_foreground;
	return has_foreground;
}


static void append_rule(GString *css, const char *selector, const GdkRGBA *background, const GdkRGBA *foreground) {
	g_autofree char *background_text = gdk_rgba_to_string(background);
	g_autofree char *foreground_text = gdk_rgba_to_string(foreground);
	g_string_append_printf(css, "%s { background-color: %s; color: %s; }\n", selector, background_text,
		foreground_text);
}


/* Sets libadwaita's colour variables for one kind of surface, such as view or headerbar */
static void append_variables(GString *css, const char *surface, const GdkRGBA *background,
	const GdkRGBA *foreground) {
	g_autofree char *background_text = gdk_rgba_to_string(background);
	g_autofree char *foreground_text = gdk_rgba_to_string(foreground);
	g_string_append_printf(css, "  --%s-bg-color: %s;\n  --%s-fg-color: %s;\n", surface, background_text, surface,
		foreground_text);
}


static void append_backdrop(GString *css, const char *surface, const GdkRGBA *background) {
	g_autofree char *background_text = gdk_rgba_to_string(background);
	g_string_append_printf(css, "  --%s-backdrop-color: %s;\n", surface, background_text);
}


static double get_luminance(const GdkRGBA *colour) {
	return 0.2126 * colour->red + 0.7152 * colour->green + 0.0722 * colour->blue;
}


/* Buttons, selections and marks take the theme's accent, with text in its own colour or one that reads on it */
static void append_accent(GString *css, const ThemeFile *file) {
	GdkRGBA accent, on_accent = {1, 1, 1, 1};
	if (!get_style_colour(file, "crimson:accent", TRUE, &accent, 0))
		return;
	gboolean is_text_darker = get_luminance(&text_foreground) < get_luminance(&text_background);
	if (!get_style_colour(file, "crimson:accent", FALSE, &on_accent, 0) && get_luminance(&accent) > BRIGHT_ACCENT)
		on_accent = is_text_darker ? text_foreground : text_background;
	append_variables(css, "accent", &accent, &on_accent);
}


static void append_window_colours(GString *css, const ThemeFile *file, gboolean is_dark) {
	GdkRGBA background, foreground;
	/* Lists and panels may sit on a grey of their own, while text views keep the code background */
	GdkRGBA window_background, window_foreground;
	get_part_colours(file, "crimson:window", 0, is_dark, &window_background, &window_foreground);
	g_string_append(css, ":root {\n");
	append_variables(css, "window", &window_background, &window_foreground);
	append_variables(css, "view", &text_background, &text_foreground);
	/* Popovers and dialogs float above the window, so they sit a little lighter in dark themes and darker in
	 * light */
	GdkRGBA raised = shade(window_background, LIFT_SHADE, !is_dark);
	append_variables(css, "popover", &raised, &window_foreground);
	append_variables(css, "dialog", &raised, &window_foreground);
	/* Cards such as settings rows sit just lighter than the window, which libadwaita would otherwise colour for its
	 * own style even when the theme is the opposite */
	GdkRGBA card = shade(window_background, is_dark ? DARK_CARD_SHADE : LIGHT_CARD_SHADE, FALSE);
	append_variables(css, "card", &card, &window_foreground);
	append_variables(css, "thumbnail", &card, &window_foreground);
	append_variables(css, "overview", &window_background, &window_foreground);
	append_accent(css, file);
	if (get_part_colours(file, "crimson:titlebar", BAR_SHADE, is_dark, &background, &foreground)) {
		append_variables(css, "headerbar", &background, &foreground);
		append_backdrop(css, "headerbar", &background);
	}
	if (get_part_colours(file, "crimson:sidebar", SIDEBAR_SHADE, is_dark, &background, &foreground)) {
		append_variables(css, "sidebar", &background, &foreground);
		append_variables(css, "secondary-sidebar", &background, &foreground);
		append_backdrop(css, "sidebar", &background);
	}
	g_string_append(css, "}\n");
	if (get_part_colours(file, "crimson:statusbar", BAR_SHADE, is_dark, &background, &foreground))
		append_rule(css, ".status-bar", &background, &foreground);
}


static void apply_themes(gboolean has_read_files) {
	gboolean is_dark = adw_style_manager_get_dark(adw_style_manager_get_default());
	const ThemeFile *file = find_theme_file(is_dark ? dark_theme : light_theme);
	if (!file)
		file = find_theme_file(is_dark ? DEFAULT_DARK_THEME : DEFAULT_LIGHT_THEME);
	has_text_colours = get_part_colours(file, "text", 0, is_dark, &text_background, &text_foreground);
	/* A light theme may be chosen for the dark style or the other way round, so shading follows the theme */
	GdkRGBA background, foreground;
	if (get_style_colour(file, "text", TRUE, &background, 0))
		is_dark = get_luminance(&background) < 0.5;
	g_autoptr(GString) css = g_string_new(NULL);
	if (has_text_colours && coloured_parts & THEME_WINDOW)
		append_window_colours(css, file, is_dark);
	/* Bars inside a toolbar view draw the window colour rather than the header bar one, so the title bar gets its
	 * colour directly, and a toolbar view marked title-bar paints it too since the bar fades while unfocused */
	if (has_text_colours && coloured_parts & THEME_TITLE_BAR
		&& get_part_colours(file, "crimson:titlebar", BAR_SHADE, is_dark, &background, &foreground))
		append_rule(css, "headerbar.title-bar, toolbarview.title-bar > .top-bar", &background, &foreground);
	if (!theme_style) {
		theme_style = gtk_css_provider_new();
		gtk_style_context_add_provider_for_display(gdk_display_get_default(), GTK_STYLE_PROVIDER(theme_style),
			GTK_STYLE_PROVIDER_PRIORITY_APPLICATION + 1);
	}
	gtk_css_provider_load_from_string(theme_style, css->str);
	for (guint index = 0; watchers && index < watchers->len; index++) {
		ThemeWatcher *watcher = &g_array_index(watchers, ThemeWatcher, index);
		watcher->on_changed(has_read_files, watcher->data);
	}
}


static void on_style_changed(GObject *object, GParamSpec *property, gpointer data) {
	apply_themes(FALSE);
}


void use_themes(const char *light_id, const char *dark_id, ThemeParts parts) {
	if (!light_theme)
		g_signal_connect(adw_style_manager_get_default(), "notify::dark", G_CALLBACK(on_style_changed), NULL);
	g_free(light_theme);
	g_free(dark_theme);
	light_theme = g_strdup(light_id ? light_id : DEFAULT_LIGHT_THEME);
	dark_theme = g_strdup(dark_id ? dark_id : DEFAULT_DARK_THEME);
	coloured_parts = parts;
	list_themes();
	apply_themes(FALSE);
}


gboolean get_theme_text_colours(GdkRGBA *background, GdkRGBA *foreground) {
	*background = text_background;
	*foreground = text_foreground;
	return has_text_colours;
}


void watch_themes(ThemeChangedFunction on_changed, gpointer data) {
	if (!watchers)
		watchers = g_array_new(FALSE, FALSE, sizeof(ThemeWatcher));
	ThemeWatcher watcher = {on_changed, data};
	g_array_append_val(watchers, watcher);
}

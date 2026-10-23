#pragma once

#include <gtk/gtk.h>

/* Where the themes all Crimson apps bundle sit among their resources */
#define BUNDLED_THEMES_RESOURCE "/io/github/CrimsonOrg/CrimsonCore/themes"

/* The themes used until one is chosen, or when the chosen one is gone */
#define DEFAULT_LIGHT_THEME "crimson-monokai-light"
#define DEFAULT_DARK_THEME "crimson-monokai"

/* Which parts of the window take the theme's colours */
typedef enum {
	/* Header bars and toolbar views with the title-bar class */
	THEME_TITLE_BAR = 1 << 0,
	/* Window, views, popovers, dialogs, cards, sidebars and the status bar */
	THEME_WINDOW = 1 << 1,
} ThemeParts;

/* A theme the user can choose, stored by id and shown by name */
typedef struct {
	char *id, *name;
} Theme;

/* Called after the theme in use was applied, with has_read_files when its folder changed on disk */
typedef void (*ThemeChangedFunction)(gboolean has_read_files, gpointer data);

/* People's own themes live where GtkSourceView editors look, so they work in other editors too */
char *get_user_theme_folder(void);
/* The user's themes, the bundled ones and GtkSourceView's own when the program loads it, sorted by name */
GPtrArray *list_themes(void);
const Theme *find_theme(const char *id);
/* Reads the theme folders again, such as after a new theme file was written */
void reload_themes(void);

/* Colours the chosen parts of all windows with the light or dark theme, whichever matches the style */
void use_themes(const char *light_id, const char *dark_id, ThemeParts parts);
/* The text colours of the theme in use, which code views and terminals take */
gboolean get_theme_text_colours(GdkRGBA *background, GdkRGBA *foreground);
void watch_themes(ThemeChangedFunction on_changed, gpointer data);

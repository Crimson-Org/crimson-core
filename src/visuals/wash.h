#pragma once

#include <gtk/gtk.h>

/* Soft blobs of colour that blur into each other under a fine still grain, drawn behind a child, such as a card's
 * place for a picture or a page's top. The colours come from a picture or from any text, such as a title */
#define CRIMSON_TYPE_WASH crimson_wash_get_type()
G_DECLARE_FINAL_TYPE(CrimsonWash, crimson_wash, CRIMSON, WASH, GtkWidget)

#define WASH_COLOR_COUNT 3

GtkWidget *create_wash(GtkWidget *child);
/* Colours of the same text are the same everywhere, so a story keeps its colours wherever it shows */
void set_wash_text(GtkWidget *wash, const char *text);
/* The picture's own most colourful parts, for a wash that matches a picture shown near it */
void set_wash_picture(GtkWidget *wash, GdkTexture *texture);
/* Places the blobs by a text, so washes of the same colours still differ, such as two stories of one site */
void set_wash_seed(GtkWidget *wash, const char *text);
/* Colours worked out elsewhere, such as from a site's icon once for all of its stories, placed by the seed */
void set_wash_colors(GtkWidget *wash, const GdkRGBA colors[WASH_COLOR_COUNT], const char *seed);
/* Draws no colours until they are set again, such as once a picture covers the whole wash */
void clear_wash(GtkWidget *wash);
/* How strongly the colours show over the background, from zero to one */
void set_wash_strength(GtkWidget *wash, float strength);
/* Fades the colours out towards the bottom and the sides, so a wash at a page's top melts into the page */
void set_wash_fading(GtkWidget *wash, gboolean is_fading);

/* The colours worked out for a text or a picture, the same that a wash would show */
void read_text_colors(const char *text, GdkRGBA colors[WASH_COLOR_COUNT]);
void read_picture_colors(GdkTexture *texture, GdkRGBA colors[WASH_COLOR_COUNT]);

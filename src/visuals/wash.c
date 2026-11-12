#include <math.h>
#include <string.h>

#include <adwaita.h>

#include "visuals/wash.h"

/* The blobs are painted this small and stretched smoothly over the widget, which blurs them for free */
#define PAINT_COLUMNS 128
#define PAINT_ROWS 96
#define BYTES_PER_PIXEL 4
#define GRAIN_SIZE 128
/* How far the grain strays from the colour, as a share of full brightness either way */
#define GRAIN_DEPTH (14.0 / 255)
/* Each colour shows as this many blobs, placed by the seed, so no two washes look alike */
#define BLOBS_PER_COLOR 2
#define BLOB_COUNT (WASH_COLOR_COUNT * BLOBS_PER_COLOR)
#define BLOB_RADIUS_MINIMUM 0.35
#define BLOB_RADIUS_RANGE 0.4
/* Colours from text sit around one hue, with neighbours this many degrees to each side */
#define HUE_SPREAD 38.0
#define DEGREES 360.0
#define TEXT_SATURATION 0.72f
#define TEXT_VALUE 0.92f
/* Muddy yellow greens look dirty as light, so text hues are spread over the rest of the circle */
#define MUDDY_HUE_START 45.0
#define MUDDY_HUE_WIDTH 55.0
/* The ground under the blobs is the first colour, this much darker, so the blobs glow out of it */
#define GROUND_VALUE 0.32f
/* A soft light falls from the top left corner, as on glass */
#define SHEEN_STRENGTH 0.22f
/* Colours from a picture are made at least this rich, so a grey photo still gives a little colour */
#define PICTURE_SATURATION_MINIMUM 0.4f
#define PICTURE_VALUE_MINIMUM 0.5f
#define PICTURE_VALUE_MAXIMUM 0.85f
/* A picture is read as this grid of averaged parts, of which the most colourful are kept */
#define SAMPLE_COLUMNS 4
#define SAMPLE_ROWS 3
#define SAMPLE_PIXELS 64
#define DEFAULT_STRENGTH 0.5f
/* How far a wash's own seed turns and lightens its colours, as shares of the circle and of full brightness */
#define HUE_SHIFT 0.07
#define VALUE_SHIFT 0.12
/* How far in from each side a fading wash reaches full strength, as the share of its width times this */
#define FADE_SIDES 3.0f
#define FADE_TOP 4.0f

struct _CrimsonWash {
	GtkWidget parent;
	GtkWidget *child;
	GdkRGBA colors[WASH_COLOR_COUNT];
	guint seed;
	float strength;
	gboolean is_fading, has_colors;
	GdkTexture *painted;
};

G_DEFINE_FINAL_TYPE(CrimsonWash, crimson_wash, GTK_TYPE_WIDGET)

static GdkTexture *grain;


static GdkTexture *get_grain(void) {
	if (grain)
		return grain;
	gsize length = GRAIN_SIZE * GRAIN_SIZE * BYTES_PER_PIXEL;
	guchar *pixels = g_malloc(length);
	GRand *random = g_rand_new_with_seed(GRAIN_SIZE);
	for (gsize index = 0; index < length; index += BYTES_PER_PIXEL) {
		double stray = g_rand_double_range(random, -GRAIN_DEPTH, GRAIN_DEPTH);
		guchar opacity = (guchar)lround(fabs(stray) * G_MAXUINT8);
		pixels[index] = pixels[index + 1] = pixels[index + 2] = stray < 0 ? 0 : opacity;
		pixels[index + 3] = opacity;
	}
	g_rand_free(random);
	g_autoptr(GBytes) bytes = g_bytes_new_take(pixels, length);
	grain = gdk_memory_texture_new(GRAIN_SIZE, GRAIN_SIZE, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED, bytes,
		GRAIN_SIZE * BYTES_PER_PIXEL);
	return grain;
}


/* Rounding can carry a share a hair past its range, and a picture of odd bits can give no number at all, which GTK
 * refuses, so every share is brought into range first */
static float to_share(float value) {
	return isfinite(value) ? CLAMP(value, 0, 1) : 0;
}


static void set_hsv(GdkRGBA *color, float hue, float saturation, float value) {
	gtk_hsv_to_rgb(to_share(hue), to_share(saturation), to_share(value), &color->red, &color->green, &color->blue);
}


void read_text_colors(const char *text, GdkRGBA colors[WASH_COLOR_COUNT]) {
	guint hash = g_str_hash(text ? text : "");
	double hue = hash % (guint)(DEGREES - MUDDY_HUE_WIDTH);
	if (hue >= MUDDY_HUE_START)
		hue += MUDDY_HUE_WIDTH;
	for (int index = 0; index < WASH_COLOR_COUNT; index++) {
		double shifted = fmod(hue + (index - 1) * HUE_SPREAD + DEGREES, DEGREES) / DEGREES;
		colors[index] = (GdkRGBA){0, 0, 0, 1};
		set_hsv(&colors[index], (float)shifted, TEXT_SATURATION, TEXT_VALUE);
	}
}


typedef struct {
	GdkRGBA color;
	float chroma;
} Sample;


static int compare_chroma(gconstpointer first, gconstpointer second) {
	const Sample *one = first, *other = second;
	return (other->chroma > one->chroma) - (other->chroma < one->chroma);
}


static GdkRGBA enrich(GdkRGBA color) {
	float hue, saturation, value;
	gtk_rgb_to_hsv(color.red, color.green, color.blue, &hue, &saturation, &value);
	set_hsv(&color, hue, MAX(saturation, PICTURE_SATURATION_MINIMUM), CLAMP(value, PICTURE_VALUE_MINIMUM,
		PICTURE_VALUE_MAXIMUM));
	color.alpha = 1;
	return color;
}


/* The picture is shrunk to a few averaged parts, and the most colourful of them become the wash */
void read_picture_colors(GdkTexture *texture, GdkRGBA colors[WASH_COLOR_COUNT]) {
	int width = gdk_texture_get_width(texture), height = gdk_texture_get_height(texture);
	gsize stride = (gsize)width * BYTES_PER_PIXEL;
	g_autofree guchar *pixels = g_malloc(stride * height);
	gdk_texture_download(texture, pixels, stride);
	Sample samples[SAMPLE_COLUMNS * SAMPLE_ROWS];
	for (int part = 0; part < SAMPLE_COLUMNS * SAMPLE_ROWS; part++) {
		int left = width * (part % SAMPLE_COLUMNS) / SAMPLE_COLUMNS, top = height * (part / SAMPLE_COLUMNS) / SAMPLE_ROWS;
		int right = width * (part % SAMPLE_COLUMNS + 1) / SAMPLE_COLUMNS;
		int bottom = height * (part / SAMPLE_COLUMNS + 1) / SAMPLE_ROWS;
		double sums[3] = {0};
		int count = 0, step = MAX(1, (right - left) / SAMPLE_PIXELS);
		for (int y = top; y < bottom; y += step)
			for (int x = left; x < right; x += step, count++) {
				/* Downloads are in the order B, G, R, A */
				const guchar *pixel = pixels + y * stride + x * BYTES_PER_PIXEL;
				sums[0] += pixel[2];
				sums[1] += pixel[1];
				sums[2] += pixel[0];
			}
		GdkRGBA color = {sums[0] / MAX(count, 1) / G_MAXUINT8, sums[1] / MAX(count, 1) / G_MAXUINT8,
			sums[2] / MAX(count, 1) / G_MAXUINT8, 1};
		float most = MAX(color.red, MAX(color.green, color.blue)), least = MIN(color.red, MIN(color.green, color.blue));
		samples[part] = (Sample){color, most - least};
	}
	qsort(samples, G_N_ELEMENTS(samples), sizeof samples[0], compare_chroma);
	for (int index = 0; index < WASH_COLOR_COUNT; index++)
		colors[index] = enrich(samples[index].color);
}


/* Lays a colour of the given strength over another */
static GdkRGBA cover(GdkRGBA below, GdkRGBA above, float strength) {
	float rest = 1 - strength;
	return (GdkRGBA){below.red * rest + above.red * strength, below.green * rest + above.green * strength,
		below.blue * rest + above.blue * strength, below.alpha * rest + strength};
}


typedef struct {
	float x, y, radius;
	GdkRGBA color;
} Blob;


/* Each wash turns its colours a little around the circle and lightens or darkens them by its seed, so washes of
 * one site's colours side by side still differ */
static GdkRGBA shift_color(GdkRGBA color, GRand *random) {
	float hue, saturation, value;
	gtk_rgb_to_hsv(color.red, color.green, color.blue, &hue, &saturation, &value);
	hue = fmodf(hue + (float)g_rand_double_range(random, -HUE_SHIFT, HUE_SHIFT) + 1, 1);
	value = CLAMP(value + (float)g_rand_double_range(random, -VALUE_SHIFT, VALUE_SHIFT), 0, 1);
	set_hsv(&color, hue, saturation, value);
	return color;
}


static void place_blobs(CrimsonWash *wash, Blob blobs[BLOB_COUNT]) {
	GRand *random = g_rand_new_with_seed(wash->seed);
	GdkRGBA shifted[WASH_COLOR_COUNT];
	for (int index = 0; index < WASH_COLOR_COUNT; index++)
		shifted[index] = shift_color(wash->colors[index], random);
	for (int index = 0; index < BLOB_COUNT; index++) {
		blobs[index].x = (float)g_rand_double(random);
		blobs[index].y = (float)g_rand_double(random);
		blobs[index].radius = (float)(BLOB_RADIUS_MINIMUM + BLOB_RADIUS_RANGE * g_rand_double(random));
		blobs[index].color = shifted[index % WASH_COLOR_COUNT];
	}
	g_rand_free(random);
}


static GdkRGBA darken(GdkRGBA color, float value) {
	float hue, saturation, brightness;
	gtk_rgb_to_hsv(color.red, color.green, color.blue, &hue, &saturation, &brightness);
	set_hsv(&color, hue, saturation, brightness * value);
	return color;
}


/* Each blob fades smoothly from its colour at its middle to nothing at its reach, glowing out of a dark ground of
 * the first colour, with a soft light from the top left */
static GdkTexture *paint(CrimsonWash *wash) {
	Blob blobs[BLOB_COUNT];
	place_blobs(wash, blobs);
	GdkRGBA base = darken(blobs[0].color, GROUND_VALUE);
	GdkRGBA light = {1, 1, 1, 1};
	gsize stride = PAINT_COLUMNS * BYTES_PER_PIXEL;
	guchar *pixels = g_malloc(stride * PAINT_ROWS);
	for (int row = 0; row < PAINT_ROWS; row++) {
		for (int column = 0; column < PAINT_COLUMNS; column++) {
			float x = (column + 0.5f) / PAINT_COLUMNS, y = (row + 0.5f) / PAINT_ROWS;
			GdkRGBA color = base;
			for (int index = 0; index < BLOB_COUNT; index++) {
				float distance = hypotf(x - blobs[index].x, y - blobs[index].y) / blobs[index].radius;
				if (distance < 1) {
					float reach = 1 - distance;
					color = cover(color, blobs[index].color, reach * reach * (3 - 2 * reach));
				}
			}
			float sheen = MAX(0, 1 - hypotf(x, y) / G_SQRT2);
			color = cover(color, light, SHEEN_STRENGTH * sheen * sheen);
			/* A fading wash melts into the page below it and at its sides */
			float side = MIN(1, MIN(x, 1 - x) * FADE_SIDES);
			float rise = MIN(1, y * FADE_TOP);
			float top = rise * rise * (3 - 2 * rise);
			float strength = wash->strength * (wash->is_fading ? (1 - y) * (1 - y) * side * top : 1);
			guchar *pixel = pixels + row * stride + column * BYTES_PER_PIXEL;
			pixel[0] = (guchar)lroundf(CLAMP(color.red * strength, 0, 1) * G_MAXUINT8);
			pixel[1] = (guchar)lroundf(CLAMP(color.green * strength, 0, 1) * G_MAXUINT8);
			pixel[2] = (guchar)lroundf(CLAMP(color.blue * strength, 0, 1) * G_MAXUINT8);
			pixel[3] = (guchar)lroundf(CLAMP(strength, 0, 1) * G_MAXUINT8);
		}
	}
	g_autoptr(GBytes) bytes = g_bytes_new_take(pixels, stride * PAINT_ROWS);
	return gdk_memory_texture_new(PAINT_COLUMNS, PAINT_ROWS, GDK_MEMORY_R8G8B8A8_PREMULTIPLIED, bytes, stride);
}


static void repaint(CrimsonWash *wash) {
	g_clear_object(&wash->painted);
	gtk_widget_queue_draw(GTK_WIDGET(wash));
}


/* Filling a recycled card again with the same story keeps the painted wash, so nothing is painted or uploaded twice */
static void take_colors(CrimsonWash *wash, const GdkRGBA colors[WASH_COLOR_COUNT], guint seed) {
	if (wash->has_colors && wash->seed == seed && memcmp(wash->colors, colors, sizeof wash->colors) == 0)
		return;
	if (colors != wash->colors)
		memcpy(wash->colors, colors, sizeof wash->colors);
	wash->seed = seed;
	wash->has_colors = TRUE;
	repaint(wash);
}


static void draw_wash(GtkWidget *widget, GtkSnapshot *snapshot) {
	CrimsonWash *wash = CRIMSON_WASH(widget);
	float width = gtk_widget_get_width(widget), height = gtk_widget_get_height(widget);
	if (wash->has_colors && width > 0 && height > 0) {
		if (!wash->painted)
			wash->painted = paint(wash);
		graphene_rect_t area = GRAPHENE_RECT_INIT(0, 0, width, height);
		gtk_snapshot_append_scaled_texture(snapshot, wash->painted, GSK_SCALING_FILTER_TRILINEAR, &area);
		/* A fading wash melts into the page without grain, since grain fading with it needs a mask, which the
		 * renderer draws into a new image every frame, a cost that shows on a page scrolled or played over it */
		if (!wash->is_fading) {
			graphene_rect_t tile = GRAPHENE_RECT_INIT(0, 0, GRAIN_SIZE, GRAIN_SIZE);
			gtk_snapshot_push_repeat(snapshot, &area, &tile);
			gtk_snapshot_append_texture(snapshot, get_grain(), &tile);
			gtk_snapshot_pop(snapshot);
		}
	}
	if (wash->child)
		gtk_widget_snapshot_child(widget, wash->child, snapshot);
}


static void measure_wash(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum, int *natural,
	int *minimum_baseline, int *natural_baseline) {
	CrimsonWash *wash = CRIMSON_WASH(widget);
	if (wash->child)
		gtk_widget_measure(wash->child, orientation, for_size, minimum, natural, minimum_baseline, natural_baseline);
}


static void allocate_wash(GtkWidget *widget, int width, int height, int baseline) {
	CrimsonWash *wash = CRIMSON_WASH(widget);
	if (wash->child)
		gtk_widget_allocate(wash->child, width, height, baseline, NULL);
}


static void dispose_wash(GObject *object) {
	CrimsonWash *wash = CRIMSON_WASH(object);
	g_clear_pointer(&wash->child, gtk_widget_unparent);
	g_clear_object(&wash->painted);
	G_OBJECT_CLASS(crimson_wash_parent_class)->dispose(object);
}


static GtkSizeRequestMode get_request_mode(GtkWidget *widget) {
	CrimsonWash *wash = CRIMSON_WASH(widget);
	return wash->child ? gtk_widget_get_request_mode(wash->child) : GTK_SIZE_REQUEST_CONSTANT_SIZE;
}


static void crimson_wash_class_init(CrimsonWashClass *class) {
	G_OBJECT_CLASS(class)->dispose = dispose_wash;
	GtkWidgetClass *widget_class = GTK_WIDGET_CLASS(class);
	widget_class->snapshot = draw_wash;
	widget_class->measure = measure_wash;
	widget_class->size_allocate = allocate_wash;
	widget_class->get_request_mode = get_request_mode;
}


static void crimson_wash_init(CrimsonWash *wash) {
	wash->strength = DEFAULT_STRENGTH;
}


GtkWidget *create_wash(GtkWidget *child) {
	CrimsonWash *wash = g_object_new(CRIMSON_TYPE_WASH, NULL);
	if (child) {
		wash->child = child;
		gtk_widget_set_parent(child, GTK_WIDGET(wash));
	}
	return GTK_WIDGET(wash);
}


void set_wash_text(GtkWidget *widget, const char *text) {
	GdkRGBA colors[WASH_COLOR_COUNT];
	read_text_colors(text, colors);
	take_colors(CRIMSON_WASH(widget), colors, g_str_hash(text ? text : ""));
}


void set_wash_seed(GtkWidget *widget, const char *text) {
	CrimsonWash *wash = CRIMSON_WASH(widget);
	take_colors(wash, wash->colors, g_str_hash(text ? text : ""));
}


void set_wash_picture(GtkWidget *widget, GdkTexture *texture) {
	CrimsonWash *wash = CRIMSON_WASH(widget);
	GdkRGBA colors[WASH_COLOR_COUNT];
	read_picture_colors(texture, colors);
	take_colors(wash, colors, wash->seed);
}


void set_wash_colors(GtkWidget *widget, const GdkRGBA colors[WASH_COLOR_COUNT], const char *seed) {
	take_colors(CRIMSON_WASH(widget), colors, g_str_hash(seed ? seed : ""));
}


void clear_wash(GtkWidget *widget) {
	CrimsonWash *wash = CRIMSON_WASH(widget);
	if (!wash->has_colors)
		return;
	wash->has_colors = FALSE;
	repaint(wash);
}


void set_wash_strength(GtkWidget *widget, float strength) {
	CRIMSON_WASH(widget)->strength = strength;
	repaint(CRIMSON_WASH(widget));
}


void set_wash_fading(GtkWidget *widget, gboolean is_fading) {
	CRIMSON_WASH(widget)->is_fading = is_fading;
	repaint(CRIMSON_WASH(widget));
}

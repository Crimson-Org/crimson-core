#include "common/text/long-lines.h"
#include "source/line-numbers.h"

#define NUMBER_PADDING_PIXELS 6
#define DECIMAL_BASE 10
/* Room for a line number of any size a file can have, inside a colour mark */
#define LINE_LABEL_BYTES 64

struct _FileLineNumbers {
	GtkSourceGutterRendererText parent_instance;
	LineNumberStyle style;
	int minimum_digits, digits;
};

#define FILE_LINE_NUMBERS_TYPE (file_line_numbers_get_type())
G_DECLARE_FINAL_TYPE(FileLineNumbers, file_line_numbers, CRIMSON, FILE_LINE_NUMBERS, GtkSourceGutterRendererText)
G_DEFINE_FINAL_TYPE(FileLineNumbers, file_line_numbers, GTK_SOURCE_TYPE_GUTTER_RENDERER_TEXT)


static int get_cursor_line(GtkTextBuffer *buffer) {
	GtkTextIter cursor;
	gtk_text_buffer_get_iter_at_mark(buffer, &cursor, gtk_text_buffer_get_insert(buffer));
	return get_joined_line(buffer, gtk_text_iter_get_line(&cursor));
}


static void file_line_numbers_query_data(GtkSourceGutterRenderer *renderer, GtkSourceGutterLines *lines,
	guint line) {
	FileLineNumbers *numbers = CRIMSON_FILE_LINE_NUMBERS(renderer);
	GtkSourceGutterRendererText *text = GTK_SOURCE_GUTTER_RENDERER_TEXT(renderer);
	GtkTextBuffer *buffer = GTK_TEXT_BUFFER(gtk_source_gutter_renderer_get_buffer(renderer));
	int file_line = (int)line;
	if (has_split_lines(buffer)) {
		GtkTextIter iter;
		gtk_text_buffer_get_iter_at_line(buffer, &iter, (int)line);
		if (continues_split_line(&iter)) {
			gtk_source_gutter_renderer_text_set_text(text, "", -1);
			return;
		}
		file_line = get_joined_line(buffer, (int)line);
	}
	const LineNumberStyle *style = &numbers->style;
	int number = style->get_number ? style->get_number(file_line, get_cursor_line(buffer), style->data)
		: file_line + 1;
	const char *colour = style->get_colour ? style->get_colour((int)line, style->data) : NULL;
	char label[LINE_LABEL_BYTES];
	if (!colour) {
		g_snprintf(label, sizeof label, "%d", number);
		gtk_source_gutter_renderer_text_set_text(text, label, -1);
		return;
	}
	g_snprintf(label, sizeof label, "<span foreground=\"%s\">%d</span>", colour, number);
	gtk_source_gutter_renderer_text_set_markup(text, label, -1);
}


static int count_number_digits(FileLineNumbers *numbers) {
	GtkSourceBuffer *buffer = gtk_source_gutter_renderer_get_buffer(GTK_SOURCE_GUTTER_RENDERER(numbers));
	int digits = 0;
	int lines = buffer ? gtk_text_buffer_get_line_count(GTK_TEXT_BUFFER(buffer)) : 1;
	for (int count = lines; count > 0; count /= DECIMAL_BASE)
		digits++;
	return MAX(digits, numbers->minimum_digits);
}


static void file_line_numbers_measure(GtkWidget *widget, GtkOrientation orientation, int for_size, int *minimum,
	int *natural, int *minimum_baseline, int *natural_baseline) {
	*minimum_baseline = *natural_baseline = -1;
	*minimum = *natural = 0;
	if (orientation == GTK_ORIENTATION_VERTICAL)
		return;
	FileLineNumbers *numbers = CRIMSON_FILE_LINE_NUMBERS(widget);
	numbers->digits = count_number_digits(numbers);
	g_autofree char *sample = g_strnfill(numbers->digits, '0');
	int width = 0;
	gtk_source_gutter_renderer_text_measure(GTK_SOURCE_GUTTER_RENDERER_TEXT(widget), sample, &width, NULL);
	*minimum = *natural = width + 2 * gtk_source_gutter_renderer_get_xpad(GTK_SOURCE_GUTTER_RENDERER(widget));
}


/* Asks for a new width only when the line count gains or loses a digit, so typing does not lay the gutter out */
static void notice_line_count(GtkTextBuffer *buffer, FileLineNumbers *numbers) {
	if (count_number_digits(numbers) != numbers->digits)
		gtk_widget_queue_resize(GTK_WIDGET(numbers));
}


/* Numbers counted from the cursor change as it moves */
static void notice_cursor(GtkSourceBuffer *buffer, FileLineNumbers *numbers) {
	if (numbers->style.get_number)
		gtk_widget_queue_draw(GTK_WIDGET(numbers));
}


static void file_line_numbers_change_buffer(GtkSourceGutterRenderer *renderer, GtkSourceBuffer *old_buffer) {
	if (old_buffer)
		g_signal_handlers_disconnect_by_data(old_buffer, renderer);
	GtkSourceBuffer *buffer = gtk_source_gutter_renderer_get_buffer(renderer);
	if (buffer) {
		g_signal_connect_object(buffer, "changed", G_CALLBACK(notice_line_count), renderer, 0);
		g_signal_connect_object(buffer, "cursor-moved", G_CALLBACK(notice_cursor), renderer, 0);
	}
	GTK_SOURCE_GUTTER_RENDERER_CLASS(file_line_numbers_parent_class)->change_buffer(renderer, old_buffer);
	gtk_widget_queue_resize(GTK_WIDGET(renderer));
}


static void file_line_numbers_class_init(FileLineNumbersClass *class) {
	GTK_WIDGET_CLASS(class)->measure = file_line_numbers_measure;
	GTK_SOURCE_GUTTER_RENDERER_CLASS(class)->query_data = file_line_numbers_query_data;
	GTK_SOURCE_GUTTER_RENDERER_CLASS(class)->change_buffer = file_line_numbers_change_buffer;
}


static void file_line_numbers_init(FileLineNumbers *numbers) {
	gtk_source_gutter_renderer_set_xalign(GTK_SOURCE_GUTTER_RENDERER(numbers), 1.0);
	gtk_source_gutter_renderer_set_xpad(GTK_SOURCE_GUTTER_RENDERER(numbers), NUMBER_PADDING_PIXELS);
}


GtkSourceGutterRenderer *add_file_line_numbers(GtkSourceView *view, int position, int minimum_digits,
	const LineNumberStyle *style) {
	FileLineNumbers *numbers = g_object_new(FILE_LINE_NUMBERS_TYPE, NULL);
	numbers->minimum_digits = minimum_digits;
	if (style)
		numbers->style = *style;
	GtkSourceGutter *gutter = gtk_source_view_get_gutter(view, GTK_TEXT_WINDOW_LEFT);
	gtk_source_gutter_insert(gutter, GTK_SOURCE_GUTTER_RENDERER(numbers), position);
	return GTK_SOURCE_GUTTER_RENDERER(numbers);
}


void forget_line_number_style(GtkSourceGutterRenderer *numbers) {
	CRIMSON_FILE_LINE_NUMBERS(numbers)->style = (LineNumberStyle){0};
}

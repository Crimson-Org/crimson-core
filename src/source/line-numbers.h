#pragma once

#include <gtksourceview/gtksource.h>

/* How a line's number shows beyond the plain count, where either function may be NULL */
typedef struct {
	/* The number to show for a file line, such as its distance from the cursor's line */
	int (*get_number)(int file_line, int cursor_line, gpointer data);
	/* A colour for the number of a buffer line, such as for a problem on it, or NULL for the scheme's own */
	const char *(*get_colour)(int buffer_line, gpointer data);
	gpointer data;
} LineNumberStyle;

/* Numbers the gutter by the file's own lines, so the later pieces of a split line show none, at the gutter position
 * given, with room for at least the digits given */
GtkSourceGutterRenderer *add_file_line_numbers(GtkSourceView *view, int position, int minimum_digits,
	const LineNumberStyle *style);
/* Stops asking the style's functions, such as when the data they read goes away first */
void forget_line_number_style(GtkSourceGutterRenderer *numbers);

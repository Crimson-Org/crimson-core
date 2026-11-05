#include <locale.h>
#include <string.h>

#include "common/text/numbers.h"

/* Languages that group digits do so in threes, apart from the few that also group in twos */
#define DIGITS_PER_GROUP 3


char *format_count(guint64 count) {
	g_autofree char *digits = g_strdup_printf("%" G_GUINT64_FORMAT, count);
	const char *separator = localeconv()->thousands_sep;
	if (!separator || !*separator)
		return g_steal_pointer(&digits);
	GString *grouped = g_string_new(NULL);
	gsize length = strlen(digits);
	for (gsize index = 0; index < length; index++) {
		if (index > 0 && (length - index) % DIGITS_PER_GROUP == 0)
			g_string_append(grouped, separator);
		g_string_append_c(grouped, digits[index]);
	}
	return g_string_free(grouped, FALSE);
}

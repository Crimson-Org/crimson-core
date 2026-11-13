#include <locale.h>
#include <string.h>

#include "common/text/numbers.h"

/* Languages that group digits do so in threes, apart from the few that also group in twos */
#define DIGITS_PER_GROUP 3
#define SECONDS_PER_MINUTE 60
#define SECONDS_PER_HOUR 3600


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


char *format_play_time(double seconds) {
	int total = MAX(0, (int)seconds);
	if (total >= SECONDS_PER_HOUR)
		return g_strdup_printf("%d:%02d:%02d", total / SECONDS_PER_HOUR, total / SECONDS_PER_MINUTE % SECONDS_PER_MINUTE,
			total % SECONDS_PER_MINUTE);
	return g_strdup_printf("%d:%02d", total / SECONDS_PER_MINUTE, total % SECONDS_PER_MINUTE);
}

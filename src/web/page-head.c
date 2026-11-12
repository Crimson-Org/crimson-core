#include <stdlib.h>
#include <string.h>

#include "web/page-head.h"
#include "web/web.h"

/* The longest named or numbered entity read, such as &hellip; or &#x1F600; */
#define LONGEST_ENTITY 10
#define WEB_PREFIX "www."
/* The head of a page sits within this many bytes */
#define LARGEST_HEAD_BYTES (256 * 1024)
#define READ_CHUNK_BYTES 16384
#define HEAD_END "</head>"
/* The size of the icon made for phones' home screens, and a size above any picture for one that scales */
#define TOUCH_ICON_SIZE 180
#define SCALABLE_ICON_SIZE 100000

/* A page being read as far as its head */
typedef struct {
	char *address;
	GInputStream *stream;
	GByteArray *head;
	GCancellable *cancellable;
	PageHeadRead read;
	gpointer data;
} PageFetch;


char *decode_html_entities(const char *text) {
	static const struct {
		const char *name, *character;
	} NAMED[] = {{"amp", "&"}, {"lt", "<"}, {"gt", ">"}, {"quot", "\""}, {"apos", "'"}, {"nbsp", " "},
		{"hellip", "..."}, {"ndash", "-"}, {"mdash", "-"}, {"rsquo", "'"}, {"lsquo", "'"}, {"rdquo", "\""},
		{"ldquo", "\""}};
	GString *decoded = g_string_new(NULL);
	for (const char *cursor = text; *cursor;) {
		const char *end = *cursor == '&' ? strchr(cursor, ';') : NULL;
		if (!end || end - cursor > LONGEST_ENTITY) {
			g_string_append_c(decoded, *cursor++);
			continue;
		}
		g_autofree char *name = g_strndup(cursor + 1, (gsize)(end - cursor - 1));
		gunichar number = *name == '#' ? (gunichar)(name[1] == 'x' || name[1] == 'X'
			? g_ascii_strtoull(name + 2, NULL, 16) : g_ascii_strtoull(name + 1, NULL, 10)) : 0;
		const char *character = NULL;
		for (guint index = 0; index < G_N_ELEMENTS(NAMED); index++)
			if (g_str_equal(name, NAMED[index].name))
				character = NAMED[index].character;
		if (number && g_unichar_validate(number))
			g_string_append_unichar(decoded, number);
		else if (character)
			g_string_append(decoded, character);
		else
			g_string_append_len(decoded, cursor, end - cursor + 1);
		cursor = end + 1;
	}
	return g_strstrip(g_string_free(decoded, FALSE));
}


/* The attributes of one tag, with names folded to lower case */
static GHashTable *read_attributes(const char *tag) {
	static GRegex *attribute;
	if (!attribute)
		attribute = g_regex_new("([a-z:_-]+)\\s*=\\s*(?:\"([^\"]*)\"|'([^']*)'|([^\\s\"'>]+))",
			G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
	GHashTable *attributes = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	g_autoptr(GMatchInfo) pairs = NULL;
	for (g_regex_match(attribute, tag, 0, &pairs); g_match_info_matches(pairs); g_match_info_next(pairs, NULL)) {
		g_autofree char *key = g_match_info_fetch(pairs, 1);
		char *value = NULL;
		for (int group = 2; group <= 4 && (!value || !*value); group++) {
			g_free(value);
			value = g_match_info_fetch(pairs, group);
		}
		g_hash_table_replace(attributes, g_ascii_strdown(key, -1), decode_html_entities(value ? value : ""));
		g_free(value);
	}
	return attributes;
}


/* Calls back with the attributes of every tag of one name, such as every meta or link tag */
static void each_tag(const char *head, const char *name, void (*found)(GHashTable *, gpointer), gpointer data) {
	g_autofree char *pattern = g_strdup_printf("<%s\\s[^>]*>", name);
	g_autoptr(GRegex) tags = g_regex_new(pattern, G_REGEX_CASELESS, 0, NULL);
	g_autoptr(GMatchInfo) match = NULL;
	for (g_regex_match(tags, head, 0, &match); g_match_info_matches(match); g_match_info_next(match, NULL)) {
		g_autofree char *tag = g_match_info_fetch(match, 0);
		g_autoptr(GHashTable) attributes = read_attributes(tag);
		found(attributes, data);
	}
}


/* Meta tags into one table by their property or name, the first of each kept */
static void keep_meta(GHashTable *attributes, gpointer data) {
	const char *name = g_hash_table_lookup(attributes, "property");
	if (!name)
		name = g_hash_table_lookup(attributes, "name");
	const char *content = g_hash_table_lookup(attributes, "content");
	if (name && content && *content && !g_hash_table_contains(data, name))
		g_hash_table_insert(data, g_ascii_strdown(name, -1), g_strdup(content));
}


static char *find_meta(GHashTable *metas, const char *const *names) {
	for (; *names; names++) {
		const char *value = g_hash_table_lookup(metas, *names);
		if (value)
			return g_strdup(value);
	}
	return NULL;
}


static char *read_title(const char *head) {
	static GRegex *title;
	if (!title)
		title = g_regex_new("<title[^>]*>([^<]*)</title>", G_REGEX_CASELESS | G_REGEX_OPTIMIZE, 0, NULL);
	g_autoptr(GMatchInfo) match = NULL;
	if (!g_regex_match(title, head, 0, &match))
		return NULL;
	g_autofree char *text = g_match_info_fetch(match, 1);
	return decode_html_entities(text);
}


static char *resolve_address(const char *page, const char *address) {
	if (!address || !*address)
		return NULL;
	return g_uri_resolve_relative(page, address, G_URI_FLAGS_NONE, NULL);
}


static gboolean is_feed_type(const char *type) {
	static const char *const TYPES[] = {"application/rss+xml", "application/atom+xml", "application/feed+json",
		"application/json", "application/rdf+xml", "application/x-rss+xml", "text/xml", "application/xml"};
	for (guint index = 0; type && index < G_N_ELEMENTS(TYPES); index++)
		if (g_ascii_strcasecmp(type, TYPES[index]) == 0)
			return TRUE;
	return FALSE;
}


static gboolean has_rel(GHashTable *attributes, const char *wanted) {
	const char *rel = g_hash_table_lookup(attributes, "rel");
	g_auto(GStrv) words = rel ? g_strsplit_set(rel, " \t", -1) : NULL;
	for (int index = 0; words && words[index]; index++)
		if (g_ascii_strcasecmp(words[index], wanted) == 0)
			return TRUE;
	return FALSE;
}


/* The largest icon the page names, since a small one shows blocky wherever it is drawn larger, with the icon made
 * for phones' home screens as the usual largest */
static void keep_icon(PageHead *head, GHashTable *attributes, const char *href) {
	const char *sizes = g_hash_table_lookup(attributes, "sizes");
	int size = has_rel(attributes, "apple-touch-icon") ? TOUCH_ICON_SIZE : sizes ? atoi(sizes) : 0;
	if (g_hash_table_lookup(attributes, "type") && strstr(g_hash_table_lookup(attributes, "type"), "svg"))
		size = SCALABLE_ICON_SIZE;
	if (head->icon && size <= head->icon_size)
		return;
	g_free(head->icon);
	head->icon = resolve_address(head->address, href);
	head->icon_size = size;
}


static void keep_link(GHashTable *attributes, gpointer data) {
	PageHead *head = data;
	const char *href = g_hash_table_lookup(attributes, "href");
	const char *type = g_hash_table_lookup(attributes, "type");
	if (!href || !*href)
		return;
	if (has_rel(attributes, "alternate") && is_feed_type(type)) {
		PageFeed *feed = g_new0(PageFeed, 1);
		feed->address = resolve_address(head->address, href);
		feed->title = g_strdup(g_hash_table_lookup(attributes, "title"));
		feed->type = g_ascii_strdown(type, -1);
		if (feed->address)
			g_ptr_array_add(head->feeds, feed);
		else
			g_free(feed);
	} else if (has_rel(attributes, "apple-touch-icon") || has_rel(attributes, "icon")) {
		keep_icon(head, attributes, href);
	}
}


static void free_page_feed(PageFeed *feed) {
	g_free(feed->address);
	g_free(feed->title);
	g_free(feed->type);
	g_free(feed);
}


PageHead *parse_page_head(const char *html, const char *address) {
	static const char *const TITLES[] = {"og:title", "twitter:title", NULL};
	static const char *const DESCRIPTIONS[] = {"og:description", "twitter:description", "description", NULL};
	static const char *const IMAGES[] = {"og:image", "og:image:url", "og:image:secure_url", "twitter:image", NULL};
	static const char *const SITES[] = {"og:site_name", "application-name", NULL};
	/* Meta tags count only in the head, while link tags name the page's own feeds wherever they are, and some
	 * sites, such as YouTube, put them in the body */
	const char *end = strstr(html, HEAD_END);
	g_autofree char *head_text = end ? g_strndup(html, (gsize)(end - html)) : g_strdup(html);
	PageHead *head = g_new0(PageHead, 1);
	head->address = g_strdup(address);
	head->feeds = g_ptr_array_new_with_free_func((GDestroyNotify)free_page_feed);
	g_autoptr(GHashTable) metas = g_hash_table_new_full(g_str_hash, g_str_equal, g_free, g_free);
	each_tag(head_text, "meta", keep_meta, metas);
	each_tag(html, "link", keep_link, head);
	head->title = find_meta(metas, TITLES);
	if (!head->title)
		head->title = read_title(head_text);
	head->description = find_meta(metas, DESCRIPTIONS);
	g_autofree char *image = find_meta(metas, IMAGES);
	head->image = resolve_address(address, image);
	head->site = find_meta(metas, SITES);
	if (!head->site) {
		g_autoptr(GUri) uri = g_uri_parse(address, G_URI_FLAGS_NONE, NULL);
		const char *host = uri ? g_uri_get_host(uri) : NULL;
		head->site = g_strdup(host && g_str_has_prefix(host, WEB_PREFIX) ? host + strlen(WEB_PREFIX) : host);
	}
	return head;
}


void free_page_head(PageHead *head) {
	if (!head)
		return;
	g_free(head->address);
	g_free(head->site);
	g_free(head->title);
	g_free(head->description);
	g_free(head->image);
	g_free(head->icon);
	g_ptr_array_unref(head->feeds);
	g_free(head);
}


static void finish_page(PageFetch *fetch, PageHead *head) {
	fetch->read(head, fetch->data);
	free_page_head(head);
	g_free(fetch->address);
	g_clear_object(&fetch->stream);
	g_clear_object(&fetch->cancellable);
	g_byte_array_unref(fetch->head);
	g_free(fetch);
}


static void read_more(PageFetch *fetch);


static void finish_reading(GObject *source, GAsyncResult *result, gpointer data) {
	PageFetch *fetch = data;
	g_autoptr(GBytes) bytes = g_input_stream_read_bytes_finish(G_INPUT_STREAM(source), result, NULL);
	gsize size = bytes ? g_bytes_get_size(bytes) : 0;
	if (size)
		g_byte_array_append(fetch->head, g_bytes_get_data(bytes, NULL), (guint)size);
	gboolean has_head_end = fetch->head->len && g_strstr_len((const char *)fetch->head->data, fetch->head->len,
		HEAD_END);
	if (size && !has_head_end && fetch->head->len < LARGEST_HEAD_BYTES) {
		read_more(fetch);
		return;
	}
	if (!fetch->head->len) {
		finish_page(fetch, NULL);
		return;
	}
	g_byte_array_append(fetch->head, (const guint8 *)"", 1);
	g_autofree char *html = g_utf8_make_valid((const char *)fetch->head->data, -1);
	finish_page(fetch, parse_page_head(html, fetch->address));
}


static void read_more(PageFetch *fetch) {
	g_input_stream_read_bytes_async(fetch->stream, READ_CHUNK_BYTES, G_PRIORITY_LOW, fetch->cancellable,
		finish_reading, fetch);
}


static void finish_asking(GObject *source, GAsyncResult *result, gpointer data) {
	PageFetch *fetch = data;
	SoupMessage *message = soup_session_get_async_result_message(SOUP_SESSION(source), result);
	fetch->stream = soup_session_send_finish(SOUP_SESSION(source), result, NULL);
	const char *type = message ? soup_message_headers_get_content_type(soup_message_get_response_headers(message),
		NULL) : NULL;
	gboolean is_page = fetch->stream && message && soup_message_get_status(message) == SOUP_STATUS_OK
		&& g_strcmp0(type, "text/html") == 0;
	if (!is_page) {
		finish_page(fetch, NULL);
		return;
	}
	/* The page's own address after redirects, which its relative addresses are read against */
	g_autofree char *final = g_uri_to_string(soup_message_get_uri(message));
	g_set_str(&fetch->address, final);
	read_more(fetch);
}


void fetch_page_head(const char *address, GCancellable *cancellable, PageHeadRead read, gpointer data) {
	PageFetch *fetch = g_new0(PageFetch, 1);
	fetch->address = g_strdup(address);
	fetch->head = g_byte_array_new();
	fetch->read = read;
	fetch->data = data;
	fetch->cancellable = cancellable ? g_object_ref(cancellable) : NULL;
	g_autoptr(SoupMessage) message = soup_message_new(SOUP_METHOD_GET, address);
	if (!message) {
		finish_page(fetch, NULL);
		return;
	}
	soup_session_send_async(get_web_session(), message, G_PRIORITY_LOW, cancellable, finish_asking, fetch);
}

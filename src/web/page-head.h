#pragma once

#include <gio/gio.h>

/* A feed a page names in its head, such as its posts or its comments */
typedef struct {
	char *address, *title, *type;
} PageFeed;

/* What a web page says about itself in its head */
typedef struct {
	char *address, *site, *title, *description, *image, *icon;
	/* The icon's size in pixels as the page names it, or zero when it names none */
	int icon_size;
	GPtrArray *feeds;
} PageHead;

/* Reads the head of a page already downloaded, with every address made absolute against the page's address */
PageHead *parse_page_head(const char *html, const char *address);
void free_page_head(PageHead *head);
G_DEFINE_AUTOPTR_CLEANUP_FUNC(PageHead, free_page_head)

/* NULL when the page could not be read or is no web page, and the head is freed once the callback returns */
typedef void (*PageHeadRead)(PageHead *head, gpointer data);
/* Downloads only as far as the end of the page's head, which is all a preview needs */
void fetch_page_head(const char *address, GCancellable *cancellable, PageHeadRead read, gpointer data);
/* Turns the character references pages use in their text back into the characters */
char *decode_html_entities(const char *text);

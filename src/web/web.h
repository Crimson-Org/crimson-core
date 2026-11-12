#pragma once

#include <libsoup/soup.h>

/* The one web session a program fetches with, which names the program to the sites it asks */
SoupSession *get_web_session(void);
/* A cookie every request to the domain carries, such as a consent choice made in advance */
void add_web_cookie(const char *name, const char *value, const char *domain);

/* What to ask for, with the version the last answer named, so an unchanged page costs no download */
typedef struct {
	const char *etag, *modified;
	/* A body that grows past this stops downloading, so no address can fill the memory */
	gsize largest_bytes;
} WebRequest;

/* What a site answered, after following its redirects */
typedef struct {
	guint status;
	/* NULL when nothing usable came, such as for a refused or unchanged page */
	GBytes *body;
	/* Where the answer finally came from, and whether every redirect on the way was permanent, which means the
	 * address given should be replaced by it */
	char *address;
	gboolean has_moved;
	char *type, *etag, *modified;
	/* How long the site asks to be left alone, from its Retry-After, or zero */
	gint64 retry_seconds;
	/* A reason in plain words when the site could not be reached at all, or NULL */
	char *failure;
} WebAnswer;

typedef void (*WebAnswered)(WebAnswer *answer, gpointer data);

/* Downloads the address on the web session, and the answer is freed once the callback returns */
void fetch_web(const char *address, const WebRequest *request, GCancellable *cancellable, WebAnswered answered,
	gpointer data);
gboolean is_web_answer_ok(const WebAnswer *answer);

#pragma once

#include <glib.h>

/* uBlock Origin's own rules for YouTube, which remove its ads and the wait it adds when they are removed, built from
 * uBlock's lists and scriptlets as they are published and kept on disk */
typedef void (*PageRulesReady)(const char *script, gpointer data);

/* Calls back once with the rules kept from the last build, or with NULL before any was built */
void read_page_rules(PageRulesReady ready, gpointer data);
/* Builds the rules for the host, such as www.youtube.com, again from uBlock's newest files when the kept ones are a day
 * old or missing, and calls back with them once built, or with NULL when nothing new came, such as while offline */
void refresh_page_rules(const char *host, PageRulesReady built, gpointer data);

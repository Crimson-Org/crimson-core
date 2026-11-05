#include <glib.h>

#include "common/app/identity.h"

static const ApplicationIdentity *current_identity;


void set_application_identity(const ApplicationIdentity *identity) {
	current_identity = identity;
}


const ApplicationIdentity *get_application_identity(void) {
	return current_identity;
}


char *get_resource_path(const char *name) {
	g_autofree char *folder = g_strdelimit(g_strdup(current_identity->id), ".", '/');
	return g_strdup_printf("/%s/%s", folder, name);
}


const char *get_user_agent(void) {
	static char *agent;
	if (!agent) {
		g_auto(GStrv) words = g_strsplit(current_identity->name, " ", -1);
		g_autofree char *joined = g_strjoinv("", words);
		agent = g_strdup_printf("%s/%s", joined, current_identity->version);
	}
	return agent;
}

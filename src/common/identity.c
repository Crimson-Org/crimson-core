#include <glib.h>

#include "identity.h"

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

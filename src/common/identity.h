#pragma once

/* Names the program uses for itself, set once at launch */
typedef struct {
	const char *id;
	const char *name;
	const char *slug;
	const char *version;
} ApplicationIdentity;

void set_application_identity(const ApplicationIdentity *identity);
const ApplicationIdentity *get_application_identity(void);

/* The program's resources, such as "/io/github/CrimsonOrg/CrimsonCode/style.css" for the name "style.css" */
char *get_resource_path(const char *name);

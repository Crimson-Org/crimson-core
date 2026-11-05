#pragma once

/* Where the files every Crimson app shares sit among the program's resources */
#define CORE_RESOURCES "/io/github/CrimsonOrg/CrimsonCore"

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
/* How the program names itself to web services, such as "CrimsonText/0.1.0", which some services ask for */
const char *get_user_agent(void);

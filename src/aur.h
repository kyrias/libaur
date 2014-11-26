#ifndef _AUR_H
#define _AUR_H

#include <sys/types.h>
#include <stdio.h>

/* basic types */
typedef struct aur_t aur_t;
typedef struct aur_request_t aur_request_t;


/* aur API */
int aur_new(aur_t **ret, const char *domainname, int secure);
void aur_free(aur_t *aur);

int aur_queue_request(aur_t *aur, aur_request_t *request);
int aur_run(aur_t *aur);


/* request API */
enum {
  REQUEST_INFO,
  REQUEST_MULTIINFO,
  REQUEST_SEARCH,
  REQUEST_MSEARCH,
  REQUEST_DOWNLOAD,
};

typedef int (*aur_request_done_fn)(aur_t *aur, aur_request_t *request, const void *response, int responselen);

int aur_request_new(aur_request_t **ret, int aur_request_type, aur_request_done_fn done_fn);
void aur_request_free(aur_request_t *request);
aur_request_t *aur_request_unref(aur_request_t *request);
aur_request_t *aur_request_ref(aur_request_t *request);

int aur_request_append_arg(aur_request_t *request, const char *arg);

void aur_request_set_userdata(aur_request_t *request, void *userdata);
void *aur_request_get_userdata(aur_request_t *request);

void aur_request_set_debug(aur_request_t *request, int debug);
int aur_request_get_debug(aur_request_t *request);

char *aur_request_get_response(aur_request_t *request);
int aur_request_get_type(aur_request_t *request);

const char *aur_request_get_url(aur_request_t *request);
int aur_request_get_http_status(aur_request_t *r);

char *const *aur_request_get_args(aur_request_t *request, int *argc);

/* package API */
struct package_t {
	char *name;
	char *description;
	char *maintainer;
	char *pkgbase;
	char *upstream_url;
	char *aur_urlpath;
	char *version;

	int category_id;
	int package_id;
	int pkgbaseid;
	int out_of_date;
	int votes;
	time_t submitted_s;
	time_t modified_s;

	char **licenses;
	char **conflicts;
	char **depends;
	char **groups;
	char **makedepends;
	char **optdepends;
	char **checkdepends;
	char **provides;
	char **replaces;
};

int aur_packages_from_json(const char *json, struct package_t **packages, int *count);
void aur_package_list_free(struct package_t *packages);
int aur_packages_format(FILE *stream, const char *format, const struct package_t **packages, void *userdata);

#endif  /* _AUR_H */

/* vim: set et ts=2 sw=2: */


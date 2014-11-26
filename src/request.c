#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#include "aur-internal.h"

static void strbuf_reset(struct strbuf_t *s) {
  if (s == NULL)
    return;

  free(s->data);
  memset(s, 0, sizeof(struct strbuf_t));
}

static int strbuf_init(struct strbuf_t *strbuf) {
  strbuf->data = calloc(1, 100);
  if (strbuf->data == NULL)
    return -ENOMEM;

  strbuf->size = 0;
  strbuf->capacity = 100;

  return 0;
}

static int strbuf_grow(struct strbuf_t *s) {
  void *newalloc;
  size_t newcap;

  newcap = s->capacity * 2.5;
  newalloc = realloc(s->data, newcap);
  if (newalloc == NULL)
    return -ENOMEM;

  s->data = newalloc;
  s->capacity = newcap;

  return 0;
}

static int strbuf_append_mem(struct strbuf_t *s, const void *data, size_t len) {
  while (len > s->capacity - s->size) {
    if (strbuf_grow(s) < 0)
      return -ENOMEM;
  }

  memcpy(&s->data[s->size], data, len);
  s->size += len;

  return 0;
}

static int strbuf_append(struct strbuf_t *s, const char *str) {
  return strbuf_append_mem(s, str, strlen(str));
}

static char *strbuf_cstr(struct strbuf_t *s) {
  if (s->data[s->size - 1] != '\0') {
    if (strbuf_append_mem(s, "", 1) < 0)
      return NULL;
  }

  return s->data;
}

static char *strbuf_steal(struct strbuf_t *s) {
  char *data = strbuf_cstr(s);

  s->capacity = 0;
  s->size = 0;
  s->data = NULL;

  return data;
}

static int strbuf_appendvf(struct strbuf_t *s, const char *format, va_list ap) {
  int len;
  va_list aq;

  va_copy(aq, ap);

  len = vsnprintf(&s->data[s->size], 0, format, aq);
  while ((size_t)len > s->capacity - s->size) {
    if (strbuf_grow(s) < 0)
      return -ENOMEM;
  }

  va_end(aq);

  s->size += vsprintf(&s->data[s->size], format, ap);

  return 0;
}

static int strbuf_appendf(struct strbuf_t *s, const char *format, ...) {
  va_list ap;
  int r;

  va_start(ap, format);
  r = strbuf_appendvf(s, format, ap);
  va_end(ap);

  return r;
}

static int strbuf_catv(struct strbuf_t *s, va_list ap) {
  const char *c;

  for (;;) {
    int r;

    c = va_arg(ap, char*);
    if (c == NULL)
      break;

    r = strbuf_append(s, c);
    if (r < 0)
      return r;
  }

  return 0;
}

static int strbuf_cat(struct strbuf_t *s, ...) {
  va_list ap;
  int r;

  va_start(ap, s);
  r = strbuf_catv(s, ap);
  va_end(ap);

  return r;
}

static int arglist_init(struct arglist_t *a) {
  a->argv = calloc(10, sizeof(char*));
  if (a->argv == NULL)
    return -ENOMEM;

  a->size = 0;
  a->capacity = 10;
  return 0;
}

static void arglist_reset(struct arglist_t *a) {
  for (size_t i = 0; i < a->size; ++i)
    free(a->argv[i]);

  free(a->argv);

  memset(a, 0, sizeof(struct arglist_t));
}

static int arglist_append(struct arglist_t *a, const char *arg) {
  if (a->size == a->capacity) {
    void *newalloc;
    size_t newcap;

    newcap = 2.5 * a->capacity;

    newalloc = realloc(a->argv, newcap * sizeof(char*));
    if (newalloc == NULL)
      return -ENOMEM;

    a->argv = newalloc;
    a->capacity = newcap;
  }

  a->argv[a->size] = strdup(arg);
  if (a->argv[a->size] == NULL)
    return -ENOMEM;

  ++a->size;
  return 0;
}

static int arglist_build_multi(struct arglist_t *a, struct strbuf_t *s) {
  int r;

  for (size_t i = 0; i < a->size; ++i) {
    _cleanup_free_ char *e = NULL;

    e = curl_easy_escape(NULL, a->argv[i], 0);
    if (e == NULL)
      return -ENOMEM;

    r = strbuf_cat(s, "&arg[]=", e, NULL);
    if (r < 0)
      return r;
  }

  return 0;
}

static int arglist_build_single(struct arglist_t *a, struct strbuf_t *s) {
  _cleanup_free_ char *e = NULL;
  int r;

  if (a->size == 0)
    return 0;

  e = curl_easy_escape(NULL, a->argv[0], 0);
  if (e == NULL)
    return -ENOMEM;

  r = strbuf_cat(s, "&arg=", e, NULL);
  if (r < 0)
    return r;

  return r;
}

static const char *rpc_method_name(int request_type) {
  switch (request_type) {
  case REQUEST_SEARCH:
    return "search";
  case REQUEST_INFO:
    return "info";
  case REQUEST_MULTIINFO:
    return "multiinfo";
  case REQUEST_MSEARCH:
    return "msearch";
  default:
    return "invalid";
  }
}

static int arglist_build(struct arglist_t *a, struct strbuf_t *s, int multi) {
  if (multi)
    return arglist_build_multi(a, s);
  else
    return arglist_build_single(a, s);
}

size_t request_write_handler_internal(void *ptr, size_t nmemb, size_t size, void *userdata) {
  struct aur_request_t *request = userdata;

  if (request->body.data == NULL && strbuf_init(&request->body) < 0)
      return 0;

  if (strbuf_append_mem(&request->body, ptr, size * nmemb))
    return 0;

  return size * nmemb;
}

int aur_request_new(aur_request_t **ret, int request_type, aur_request_done_fn done_fn) {
  aur_request_t *r;

  r = calloc(1, sizeof(aur_request_t));
  if (r == NULL)
    return -ENOMEM;

  r->curl = curl_easy_init();
  if (r->curl == NULL)
    return -ENOMEM;

  if (arglist_init(&r->args) < 0)
    return -ENOMEM;

  r->refcount = 1;
  r->request_type = request_type;
  r->done_fn = done_fn;

  *ret = r;
  return 0;
}

static int request_build_rpc(aur_request_t *request, int rpc_version, struct strbuf_t *s) {
  int r;

  r = strbuf_appendf(s, "/rpc.php?v=%d&type=%s", rpc_version,
      rpc_method_name(request->request_type));
  if (r < 0)
    return r;

  r = arglist_build(&request->args, s,
      request->request_type == REQUEST_MULTIINFO);
  if (r < 0)
    return r;

  request->url = strbuf_steal(s);
  return 0;
}

static int request_build_download(aur_request_t *request, struct strbuf_t *s) {
  int r;

  r = strbuf_append(s, request->args.argv[0]);
  if (r < 0)
    return r;

  request->url = strbuf_steal(s);
  return 0;
}

int request_build_internal(aur_request_t *request, const char *protocol, const char *domain,
    int rpc_version) {
  struct strbuf_t s;
  int r;

  r = strbuf_init(&s);
  if (r < 0)
    return r;

  r = strbuf_cat(&s, protocol, "://", domain, NULL);
  if (r < 0)
    return r;

  if (request->request_type == REQUEST_DOWNLOAD)
    return request_build_download(request, &s);
  else
    return request_build_rpc(request, rpc_version, &s);
}

void aur_request_free(aur_request_t *request) {
  if (request == NULL)
    return;

  curl_easy_cleanup(request->curl);

  arglist_reset(&request->args);
  strbuf_reset(&request->body);

  free(request->url);
  free(request);
}

aur_request_t *aur_request_unref(aur_request_t *request) {
  --request->refcount;

  if (request->refcount == 0) {
    aur_request_free(request);
    request = NULL;
  }

  return request;
}

aur_request_t *aur_request_ref(aur_request_t *request) {
  ++request->refcount;
  return request;
}

void aur_request_set_userdata(aur_request_t *request, void *userdata) {
  request->userdata = userdata;
}

void *aur_request_get_userdata(aur_request_t *request) {
  return request->userdata;
}

void aur_request_set_debug(aur_request_t *request, int debug) {
  request->debug = debug;
}

int aur_request_get_debug(aur_request_t *request) {
  return request->debug;
}

int aur_request_append_arg(aur_request_t *request, const char *arg) {
  return arglist_append(&request->args, arg);
}

char *aur_request_get_response(aur_request_t *request) {
  return strbuf_steal(&request->body);
}

int aur_request_get_type(aur_request_t *request) {
  return request->request_type;
}

const char *aur_request_get_url(aur_request_t *request) {
  const char *url;

  curl_easy_getinfo(request->curl, CURLINFO_EFFECTIVE_URL, &url);

  return url;
}

int aur_request_get_http_status(aur_request_t *request) {
  long status;

  curl_easy_getinfo(request->curl, CURLINFO_RESPONSE_CODE, &status);

  return (int) status;
}

char *const *aur_request_get_args(aur_request_t *request, int *argc) {
  *argc = request->args.size;

  return request->args.argv;
}

/* vim: set et ts=2 sw=2: */


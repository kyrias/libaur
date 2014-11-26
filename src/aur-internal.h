#ifndef _AUR_INTERNAL_H
#define _AUR_INTERNAL_H

#include <stdlib.h>

#include <curl/curl.h>

#include "aur.h"
#include "macro.h"

struct aur_t {
  const char *proto;
  char *domainname;
  int version;

  int active_requests;
  CURLM *curlm;
};

struct arglist_t {
  char **argv;
  size_t size;
  size_t capacity;
};

struct strbuf_t {
  char *data;
  size_t size;
  size_t capacity;
};

struct aur_request_t {
  int request_type;
  struct arglist_t args;
  char *url;

  CURL *curl;
  struct strbuf_t body;
  aur_request_done_fn done_fn;

  int refcount;

  int debug;
  void *userdata;
};

int request_build_internal(aur_request_t *request, const char *protocol, const char *domain, int rpc_version);
size_t request_write_handler_internal(void *ptr, size_t nmemb, size_t size, void *userdata);

#endif  /* _AUR_INTERNAL_H */

/* vim: set et ts=2 sw=2: */


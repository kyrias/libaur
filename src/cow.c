#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <archive.h>
#include <archive_entry.h>

#include "aur.h"
#include "macro.h"

static int tarball_extract_from_mem(const void *data, size_t len) {
  struct archive *a;
  struct archive_entry *hdr;
  const int extract_mask = ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_TIME;
  int r = ARCHIVE_OK;

  a = archive_read_new();

  archive_read_support_format_all(a);
  archive_read_support_filter_all(a);

  /* Ugly cast, but harmless. libarchive doesn't actually modify the memory for
   * read operations -- it's just an API wart. */
  if (archive_read_open_memory(a, (void*)data, len) != ARCHIVE_OK) {
    r = -archive_errno(a);
    goto done;
  }

  while (archive_read_next_header(a, &hdr) == ARCHIVE_OK) {
    if (archive_read_extract(a, hdr, extract_mask) != ARCHIVE_OK) {
      r = -archive_errno(a);
      goto done;
    }
  }

done:
  archive_read_close(a);
  archive_read_free(a);

  return r;
}

static void dump_string(const char *k, const char *v) {
  if (v == NULL)
    return;

  printf("%-15s: %s\n", k, v);
}

static void dump_int(const char *k, int v) {
  printf("%-15s: %d\n", k, v);
}

static void dump_stringlist(const char *k, char **l) {
  if (l == NULL)
    return;

  printf("%-15s:", k);
  for (char **s = l; *s; ++s)
    printf(" %s", *s);
  fputc('\n', stdout);
}

static void dump_bool(const char *k, int b) {
  dump_string(k, b ? "Yes" : "No");
}

static void dump_time(const char *k, time_t t) {
  char buf[BUFSIZ];
  struct tm ts;

  localtime_r(&t, &ts);
  strftime(buf, sizeof(buf), "%c", &ts);
  dump_string(k, buf);
}

static void dump_package(struct package_t *p) {
  dump_string("Repository", "aur");
  dump_string("Name", p->name);
  dump_string("Version", p->version);
  dump_string("URL", p->upstream_url);
  dump_stringlist("Depends", p->depends);
  dump_stringlist("OptDepends", p->optdepends);
  dump_stringlist("Makedepends", p->makedepends);
  dump_stringlist("Provides", p->provides);
  dump_stringlist("Conflicts With", p->conflicts);
  dump_stringlist("Replaces", p->replaces);
  dump_int("Category", p->category_id);
  dump_stringlist("Licenses", p->licenses);
  dump_int("Votes", p->votes);
  dump_bool("Out Of Date", p->out_of_date);
  dump_string("Maintainer", p->maintainer);
  dump_time("Submitted", p->submitted_s);
  dump_time("Last Modified", p->modified_s);
  dump_string("Description", p->description);
  fputc('\n', stdout);
}

static void dump_package_search(struct package_t *p) {
  printf("aur/%s %s (%d)\n    %s\n", p->name, p->version, p->votes, p->description);
}

static int done_cb_json(aur_t *aur, aur_request_t *req, const void *response, int responselen) {
  struct package_t *pkgs;
  int r, c;
  void (*dumpfn)(struct package_t*);

  (void)aur; (void)responselen;

  if (aur_request_get_type(req) == REQUEST_SEARCH)
    dumpfn = dump_package_search;
  else
    dumpfn = dump_package;

  r = aur_packages_from_json(response, &pkgs, &c);
  if (r < 0)
    fprintf(stderr, "failed to decode json\n");

  if (c == 0)
    fprintf(stderr, "error: no results\n");

  for (int i = 0; i < c; ++i)
    dumpfn(&pkgs[i]);

  aur_package_list_free(pkgs);

  aur_request_unref(req);

  return 0;
}

static int done_cb_blob(aur_t *aur, aur_request_t *req, const void *response, int responselen) {
  int r;

  (void)aur;

  r = tarball_extract_from_mem(response, responselen);
  if (r < 0)
    fprintf(stderr, "error: failed to extract tarball to disk: %s\n", strerror(-r));

  aur_request_unref(req);

  return r;
}

static int ready_for_download(aur_t *aur, aur_request_t *req, const void *response, int responselen) {
  struct package_t *pkgs;
  int r, c;

  (void)responselen;

  aur_request_unref(req);

  r = aur_packages_from_json(response, &pkgs, &c);
  if (r < 0) {
    fprintf(stderr, "failed to decode json\n");
    return 1;
  }

  if (c == 0) {
    fprintf(stderr, "no results found\n");
    return 0;
  }

  for (int i = 0; i < c; ++i) {
    aur_request_t *dlreq;

    r = aur_request_new(&dlreq, REQUEST_DOWNLOAD, done_cb_blob);
    if (r < 0) {
      fprintf(stderr, "failed to make download request\n");
      return 1;
    }

    r = aur_request_append_arg(dlreq, pkgs[i].aur_urlpath);
    if (r < 0) {
      fprintf(stderr, "failed to make append urlpath\n");
      return 1;
    }

    r = aur_queue_request(aur, dlreq);
    if (r < 0) {
      fprintf(stderr, "failed to make queue download request\n");
      return 1;
    }
  }

  aur_package_list_free(pkgs);

  return 0;
}

static int string_to_aur_request_type(const char *str) {
  if (strcmp(str, "info") == 0)
    return REQUEST_INFO;
  if (strcmp(str, "multiinfo") == 0)
    return REQUEST_MULTIINFO;
  if (strcmp(str, "search") == 0)
    return REQUEST_SEARCH;
  if (strcmp(str, "msearch") == 0)
    return REQUEST_MSEARCH;
  if (strcmp(str, "download") == 0)
    return REQUEST_DOWNLOAD;

  return -1;
}

static aur_request_done_fn get_callback_for_method(int request_type) {
  switch (request_type) {
  case REQUEST_INFO:
  case REQUEST_MULTIINFO:
  case REQUEST_SEARCH:
  case REQUEST_MSEARCH:
    return done_cb_json;
  case REQUEST_DOWNLOAD:
    return done_cb_blob;
  default:
    return NULL;
  }
}

static int build_download_request(int argc, char **argv, aur_request_t ***_r, int *rc) {
  aur_request_t **r;

  r = malloc(sizeof(aur_request_t*));
  if (r == NULL)
    return -ENOMEM;

  if (aur_request_new(&r[0], REQUEST_MULTIINFO, ready_for_download) < 0)
    return -ENOMEM;

  for (int i = 0; i < argc; ++i) {
    if (aur_request_append_arg(r[0], argv[i]) < 0)
      return -ENOMEM;
  }

  *_r = r;
  *rc = 1;
  return 0;
}

static int build_rpc_request_multiarg(int argc, char **argv, int method, aur_request_t ***_r, int *rc) {
  aur_request_t **r;
  aur_request_done_fn done_cb;

  done_cb = get_callback_for_method(method);

  r = malloc(1 * sizeof(aur_request_t*));
  if (r == NULL)
    return -ENOMEM;

  if (aur_request_new(&r[0], method, done_cb) < 0)
    return -ENOMEM;

  for (int i = 0; i < argc; ++i)
    if (aur_request_append_arg(r[0], argv[i]) < 0)
      return -ENOMEM;

  *rc = 1;
  *_r = r;
  return 0;
}

static int build_rpc_request_singlearg(int argc, char **argv, int method, aur_request_t ***_r, int *rc) {
  aur_request_t **r;
  aur_request_done_fn done_cb;

  done_cb = get_callback_for_method(method);

  r = malloc(1 * sizeof(aur_request_t*));
  if (r == NULL)
    return -ENOMEM;


  for (int i = 0; i < argc; ++i) {
    if (aur_request_new(&r[i], method, done_cb) < 0)
      return -ENOMEM;

    if (aur_request_append_arg(r[i], argv[i]) < 0)
      return -ENOMEM;
  }

  *rc = argc;
  *_r = r;
  return 0;
}

static int build_rpc_requests(int argc, char **argv, int method, aur_request_t ***_r, int *rc) {
  if (method == REQUEST_MULTIINFO)
    return build_rpc_request_multiarg(argc, argv, method, _r, rc);
  else
    return build_rpc_request_singlearg(argc, argv, method, _r, rc);
}

static int build_requests(int argc, char **argv, int method, aur_request_t ***_r, int *rc) {
  if (method == REQUEST_DOWNLOAD)
    return build_download_request(argc, argv, _r, rc);
  else
    return build_rpc_requests(argc, argv, method, _r, rc);
}

static int queue_requests(aur_t *aur, aur_request_t **reqs, int rc) {
  for (int i = 0; i < rc; ++i) {
    int r;

    r = aur_queue_request(aur, reqs[i]);
    if (r < 0) {
      fprintf(stderr, "error: aur_queue_request failed: %s\n", strerror(-r));
      return 1;
    }
  }

  return 0;
}

static void usage(FILE *stream, const char *argv0) {
  fprintf(stream, "usage: %s action packages...\n\n", argv0);
  fprintf(stream,
         "Actions:\n"
         "   info                  show package info\n"
         "   multiinfo             show package info\n"
         "   search                show package search results\n"
         "   msearch               show maintainer search results\n"
         "   download              download packages\n");
}

int main(int argc, char **argv) {
  _cleanup_free_ aur_request_t **reqs = NULL;
  aur_t *aur;
  int rc, r, t;

  if (argc < 3 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    usage(stdout, argv[0]);
    return 0;
  }

  if (argc < 3) {
    usage(stderr, argv[0]);
    return 1;
  }

  t = string_to_aur_request_type(argv[1]);
  if (t < 0) {
    fprintf(stderr, "error: unknown request type: %s\n", argv[1]);
    return 1;
  }

  r = aur_new(&aur, AUR_DOMAIN, 1);
  if (r < 0) {
    fprintf(stderr, "error: aur_new failed: %s\n", strerror(-r));
    return 1;
  }

  r = build_requests(argc - 2, argv + 2, t, &reqs, &rc);
  if (r < 0) {
    fprintf(stderr, "error: build_requests failed: %s\n", strerror(-r));
    return 1;
  }

  r = queue_requests(aur, reqs, rc);
  if (r < 0) {
    fprintf(stderr, "error: queue_requests failed: %s\n", strerror(-r));
    return 1;
  }

  r = aur_run(aur);
  if (r < 0) {
    fprintf(stderr, "error: aur_run failed: %s\n", strerror(-r));
    return 1;
  }

  aur_free(aur);

  return 0;
}

/* vim: set et ts=2 sw=2: */


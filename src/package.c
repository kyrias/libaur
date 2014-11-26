#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>

#include <yajl_tree.h>

#include "aur-internal.h"
#include "macro.h"

#define MAX_FORMAT_LEN 64

static char const g_digits[] = "0123456789";
static char const g_printf_flags[] = "'-+ #0I";

struct json_descriptor_t {
  const char *key;
  yajl_type type;
  size_t offset;
};

static int json_map_key_cmp(const void *a, const void *b) {
  const struct json_descriptor_t *j = a, *k = b;

  return strcmp(j->key, k->key);
}

static const struct json_descriptor_t *descmap_get_key(struct json_descriptor_t table[], size_t tabsize, const char *key) {
  struct json_descriptor_t needle;

  needle.key = key;

  return bsearch(&needle, table, tabsize, sizeof(struct json_descriptor_t), json_map_key_cmp);
}

static void copy_to_string(yajl_val node, char **s) {
  *s = strdup(node->u.string);
}

static void copy_to_integer(yajl_val node, int *i) {
  *i = node->u.number.i;
}

static void copy_to_array(yajl_val node, char ***l) {
  char **t;

  t = calloc(node->u.array.len + 1, sizeof(char*));
  if (t == NULL)
    return;

  for (size_t i = 0; i < node->u.array.len; ++i)
    copy_to_string(node->u.array.values[i], &t[i]);

  *l = t;
  return;
}

static void copy_to_object(yajl_val node, struct json_descriptor_t table[], size_t tabsize, uint8_t *output_base) {
  for (size_t i = 0; i < YAJL_GET_OBJECT(node)->len; ++i) {
    const char *k = YAJL_GET_OBJECT(node)->keys[i];
    yajl_val v = YAJL_GET_OBJECT(node)->values[i];
    void *dest;

    const struct json_descriptor_t *json_desc = descmap_get_key(table, tabsize, k);
    if (json_desc == NULL) {
      fprintf(stderr, "error: lookup failed for key=%s\n", k);
      continue;
    }

    /* don't handle this, just leave the field empty */
    if (v->type == yajl_t_null)
      continue;

    if (v->type != json_desc->type) {
      fprintf(stderr, "error: type mismatch for key=%s: got=%d, expected=%d\n", k, v->type, json_desc->type);
      continue;
    }

    dest = output_base + json_desc->offset;
    switch (v->type) {
    case yajl_t_string:
      copy_to_string(v, dest);
      break;
    case yajl_t_number:
      copy_to_integer(v, dest);
      break;
    case yajl_t_array:
      copy_to_array(v, dest);
      break;
    default:
      printf("unhandled type %d for key %s\n", v->type, k);
    }
  }
}

static void free_strv(char **strv) {
  if (strv == NULL)
    return;

  for (char **s = strv; *s; ++s)
    free(*s);

  free(strv);
}

static void package_reset(struct package_t *package) {
  free(package->name);
  free(package->description);
  free(package->maintainer);
  free(package->pkgbase);
  free(package->upstream_url);
  free(package->aur_urlpath);
  free(package->version);

  free_strv(package->licenses);
  free_strv(package->conflicts);
  free_strv(package->depends);
  free_strv(package->groups);
  free_strv(package->makedepends);
  free_strv(package->optdepends);
  free_strv(package->checkdepends);
  free_strv(package->provides);
  free_strv(package->replaces);
}

void aur_package_list_free(struct package_t *packages) {
  for (struct package_t *p = packages; p->name; ++p)
    package_reset(p);

  free(packages);
}

int aur_packages_from_json(const char *json, struct package_t **packages, int *count) {
  yajl_val node, results;
  char error_buffer[1024];
  const char *path[] = { "results", NULL };
  struct package_t *p;

  struct json_descriptor_t table[] = {
    {"CategoryID",      yajl_t_number, offsetof(struct package_t, category_id) },
    {"Conflicts",       yajl_t_array,  offsetof(struct package_t, conflicts) },
    {"Depends",         yajl_t_array,  offsetof(struct package_t, depends) },
    {"Description",     yajl_t_string, offsetof(struct package_t, description) },
    {"FirstSubmitted",  yajl_t_number, offsetof(struct package_t, submitted_s) },
    {"Groups",          yajl_t_array,  offsetof(struct package_t, groups) },
    {"ID",              yajl_t_number, offsetof(struct package_t, package_id) },
    {"LastModified",    yajl_t_number, offsetof(struct package_t, modified_s) },
    {"License",         yajl_t_array,  offsetof(struct package_t, licenses) },
    {"Maintainer",      yajl_t_string, offsetof(struct package_t, maintainer) },
    {"MakeDepends",     yajl_t_array,  offsetof(struct package_t, makedepends) },
    {"Name",            yajl_t_string, offsetof(struct package_t, name) },
    {"NumVotes",        yajl_t_number, offsetof(struct package_t, votes) },
    {"OptDepends",      yajl_t_array,  offsetof(struct package_t, optdepends) },
    {"OutOfDate",       yajl_t_number, offsetof(struct package_t, out_of_date) },
    {"PackageBase",     yajl_t_string, offsetof(struct package_t, pkgbase) },
    {"PackageBaseID",   yajl_t_number, offsetof(struct package_t, pkgbaseid) },
    {"Provides",        yajl_t_array,  offsetof(struct package_t, provides) },
    {"Replaces",        yajl_t_array,  offsetof(struct package_t, licenses) },
    {"URL",             yajl_t_string, offsetof(struct package_t, upstream_url) },
    {"URLPath",         yajl_t_string, offsetof(struct package_t, aur_urlpath) },
    {"Version",         yajl_t_string, offsetof(struct package_t, version) },
  };

  node = yajl_tree_parse(json, error_buffer, sizeof(error_buffer));
  if (node == NULL) {
    fprintf(stderr, "json parse fail: %s\n", error_buffer);
    return -EINVAL;
  }

  results = yajl_tree_get(node, path, yajl_t_array);
  if (!YAJL_IS_ARRAY(results)) {
    fprintf(stderr, "error: json type mismatch\n");
    return -EBADMSG;
  }

  p = calloc(results->u.array.len + 1, sizeof(struct package_t));
  if (p == NULL)
    return -ENOMEM;

  for (size_t i = 0; i < results->u.array.len; ++i)
    copy_to_object(results->u.array.values[i], table, ARRAYSIZE(table), (uint8_t*)&p[i]);
  p[results->u.array.len].name = NULL;

  *packages = p;
  *count = results->u.array.len;

  yajl_tree_free(node);

  return 0;
}

typedef void (*specifier_format_fn)(FILE *stream, const char *format, void *data, void *userdata);

static void specifier_format_str(FILE *stream, const char *format, void *data, void *userdata) {
  (void) userdata;
  fprintf(stream, format, (const char *)data);
}

struct specifier_t {
  char rune;
  char output_format;
  specifier_format_fn format;
  size_t offset;
};

static int specifier_cmp(const void *a, const void *b) {
  const struct specifier_t *j = a, *k = b;

  return j->rune - k->rune;
}

static const struct specifier_t *specmap_get_key(struct specifier_t table[], size_t tabsize, char rune) {
  struct specifier_t needle;

  needle.rune = rune;

  return bsearch(&needle, table, tabsize, sizeof(struct specifier_t), specifier_cmp);
}

static int aur_package_format(FILE *stream, const char *format, const struct package_t *package, void *userdata) {
  struct specifier_t table[] = {
    { 'C', 's', specifier_format_str, offsetof(struct package_t, conflicts) },
    { 'D', 's', specifier_format_str, offsetof(struct package_t, depends) },
    { 'M', 's', specifier_format_str, offsetof(struct package_t, makedepends) },
    { 'O', 's', specifier_format_str, offsetof(struct package_t, optdepends) },
    { 'P', 's', specifier_format_str, offsetof(struct package_t, provides) },
    { 'R', 's', specifier_format_str, offsetof(struct package_t, replaces) },
    { 'a', 'd', specifier_format_str, offsetof(struct package_t, modified_s) },
    { 'c', 'd', specifier_format_str, offsetof(struct package_t, category_id) },
    { 'd', 's', specifier_format_str, offsetof(struct package_t, description) },
    { 'i', 's', specifier_format_str, offsetof(struct package_t, package_id) },
    { 'l', 's', specifier_format_str, offsetof(struct package_t, licenses) },
    { 'm', 's', specifier_format_str, offsetof(struct package_t, maintainer) },
    { 'n', 's', specifier_format_str, offsetof(struct package_t, name) },
    { 'o', 'd', specifier_format_str, offsetof(struct package_t, votes) },
    { 'p', 's', specifier_format_str, offsetof(struct package_t, aur_urlpath) },
    { 's', 'd', specifier_format_str, offsetof(struct package_t, submitted_s) },
    { 't', 'd', specifier_format_str, offsetof(struct package_t, out_of_date) },
    { 'u', 's', specifier_format_str, offsetof(struct package_t, upstream_url) },
    { 'v', 's', specifier_format_str, offsetof(struct package_t, version) },
  };

  const char *end;

  end = format + strlen(format);

  for (const char *f = format; f < end; ++f) {
    if (*f == '%') {
      char fmt[MAX_FORMAT_LEN] = { 0 };
      int l = 1;
      const struct specifier_t *spec;

      l += strspn(f + l, g_printf_flags);
      l += strspn(f + l, g_digits);
      memcpy(fmt, f, l);

      f += l;

      spec = specmap_get_key(table, ARRAYSIZE(table), *f);
      if (spec != NULL) {
        fmt[l] = spec->output_format;
        spec->format(stream, fmt, (uint8_t*)package + spec->offset, userdata);
      } else
        fputc('?', stream);
    } else if (*f == '\\') {
      /* FIXME: implement escaping */
      fputc(*f, stream);
    } else {
      fputc(*f, stream);
    }
  }

  return 0;
}

int aur_packages_format(FILE *stream, const char *format, const struct package_t **packages, void *userdata) {
  for (const struct package_t **p = packages; *p; ++p)
    aur_package_format(stream, format, *p, userdata);

  return 0;
}

/* vim: set et ts=2 sw=2: */


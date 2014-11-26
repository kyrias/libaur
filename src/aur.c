#include <errno.h>
#include <string.h>
#include <stdlib.h>

#include "aur-internal.h"

int aur_new(aur_t **ret, const char *domainname, int secure) {
  aur_t *aur;

  aur = calloc(1, sizeof(*aur));
  if (aur == NULL)
    return -ENOMEM;

  aur->version = 3;
  aur->proto = secure ? "https" : "http";

  aur->domainname = strdup(domainname);
  if (aur->domainname == NULL)
    return -ENOMEM;

  if (curl_global_init(CURL_GLOBAL_ALL) != CURLE_OK)
    return -ENOMEM;

  aur->curlm = curl_multi_init();
  if (aur->curlm == NULL)
    return -ENOMEM;

  *ret = aur;

  return 0;
}

void aur_free(aur_t *aur) {
  if (aur == NULL)
    return;

  curl_multi_cleanup(aur->curlm);
  curl_global_cleanup();

  free(aur->domainname);
  free(aur);
}

int aur_queue_request(aur_t *aur, aur_request_t *request) {
  int r;

  r = request_build_internal(request, aur->proto, aur->domainname, aur->version);
  if (r < 0)
    return r;

  curl_easy_setopt(request->curl, CURLOPT_URL, request->url);
  curl_easy_setopt(request->curl, CURLOPT_PRIVATE, request);
  curl_easy_setopt(request->curl, CURLOPT_ENCODING, "deflate,gzip");
  curl_easy_setopt(request->curl, CURLOPT_WRITEDATA, request);
  curl_easy_setopt(request->curl, CURLOPT_WRITEFUNCTION, request_write_handler_internal);
  curl_easy_setopt(request->curl, CURLOPT_VERBOSE, (long)request->debug);

  curl_multi_add_handle(aur->curlm, request->curl);
  ++aur->active_requests;

  return 0;
}

static int dispatch_finished_requests(aur_t *aur) {
  int msgs_left, abort = 0;

  do {
    CURLMsg *msg;

    msg = curl_multi_info_read(aur->curlm, &msgs_left);
    if (msg == NULL)
      break;

    /* curl doesn't define any other msg than CURLMSG_DONE, but to be safe... */
    if (msg->msg == CURLMSG_DONE) {
      aur_request_t *r;
      double content_len;

      --aur->active_requests;

      if (msg->data.result != CURLE_OK)
        fprintf(stderr, "error: request failed: %s\n", curl_easy_strerror(msg->data.result));

      curl_easy_getinfo(msg->easy_handle, CURLINFO_PRIVATE, (const char **)&r);
      curl_easy_getinfo(msg->easy_handle, CURLINFO_CONTENT_LENGTH_DOWNLOAD, &content_len);

      r = aur_request_ref(r);

      if (r->done_fn && r->done_fn(aur, r, aur_request_get_response(r), content_len) != 0) {
        printf("user signaled abort\n");
        abort = 1;
      }

      curl_multi_remove_handle(aur->curlm, msg->easy_handle);

      r = aur_request_unref(r);
    }
  } while (msgs_left);

  return abort;
}

int aur_run(aur_t *aur) {
  int active;

  do {
    int r, n;

    r = curl_multi_perform(aur->curlm, &active);
    if (r != CURLE_OK)
      return -r;

    r = curl_multi_wait(aur->curlm, NULL, 0, 1000, &n);
    if (r != CURLE_OK)
      return -r;

    r = dispatch_finished_requests(aur);
    if (r != 0)
      return r;
  } while (aur->active_requests > 0);

  return 0;
}

/* vim: set et ts=2 sw=2: */


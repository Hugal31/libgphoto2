#include <stdlib.h>
#include <string.h>

#include <curl/curl.h>
#include <json-c/json.h>

#include <gphoto2/gphoto2-library.h>
#include <gphoto2/gphoto2-result.h>

#include "ccapi.h"
#include "requests.h"

#define CURL_CHECK(op) { int ret = op; if (ret != CURLE_OK) return curl_error_to_gp(ret); }
#define CURL_CHECK_GOTO(op, label) { int curl_ret = op; if (curl_ret != CURLE_OK) { ret = curl_error_to_gp(curl_ret); goto label; } }
#define CURL_CHECK_GOTO_LOG(op, msg, label) { \
    int curl_ret = op; \
    if (curl_ret != CURLE_OK) { \
        fprintf(stderr, "%s: %s\n", msg, curl_easy_strerror(curl_ret)); \
        ret = curl_error_to_gp(curl_ret); \
        goto label; \
    } \
}

void buffer_init(struct Buffer *buffer)
{
    buffer->data = NULL;
    buffer->size = 0;
}

void buffer_destroy(struct Buffer *buffer)
{
    free(buffer->data);
}

int buffer_realloc(struct Buffer *buffer, size_t new_size)
{
    char *ptr = realloc(buffer->data, new_size);
    if (!ptr)
        return GP_ERROR_NO_MEMORY;
    buffer->data = ptr;
    buffer->size = new_size;
    return GP_OK;
}

/// @brief Curl write callback.
/// @note Add a trailing '\0' to the data.
/// @param contents The content to copy
/// @param size The size of the items.
/// @param nmemb The number of items.
/// @param userp A pointer to a struct Buffer.
/// @return The number of byte written.
/// @see https://curl.se/libcurl/c/getinmemory.html
static size_t
write_to_buffer(void *contents, size_t size, size_t nmemb, void *userp)
{
  size_t realsize = size * nmemb;
  struct Buffer *mem = (struct Buffer *)userp;
 
  char *ptr = realloc(mem->data, mem->size + realsize + 1);
  if(!ptr) {
    /* out of memory! */
    printf("not enough memory (realloc returned NULL)\n");
    return 0;
  }
 
  mem->data = ptr;
  memcpy(&(mem->data[mem->size]), contents, realsize);
  mem->size += realsize;
  mem->data[mem->size] = 0;
 
  return realsize;
}

static int curl_error_to_gp(int curle)
{
	switch (curle)
	{
		case CURLE_OK: return GP_OK;
        case CURLE_OUT_OF_MEMORY: return GP_ERROR_NO_MEMORY;
		default: return GP_ERROR_IO;
	}
}

/// @brief Set CURLOPT_URL using the port and the given \p path.
static int set_url(CURL *curl, Camera *camera, const char *path)
{
	GPPortInfo portInfo;
	char *scheme = "";
	char *portPath;
	const char *baseUrl;

	GP_CHECK(gp_port_get_info(camera->port, &portInfo));
	GP_CHECK(gp_port_info_get_path(portInfo, &portPath));

	const char *firstColonLoc = strchr(portPath, ':');
	if (!firstColonLoc || !*(firstColonLoc + 1))
		return GP_ERROR_BAD_PARAMETERS;

	baseUrl = firstColonLoc + 1;
	const char *schemeLoc = strstr(baseUrl, "://");
	if (!schemeLoc)
	{
		const char *secondColonLoc = strchr(baseUrl, ':');
		if (secondColonLoc)
		{
			char *numEnd;
			unsigned long long port = strtoull(secondColonLoc + 1, &numEnd, 10);
			if (port == 443)
				scheme = "https://";
		}
	}

	size_t urlSize = strlen(scheme) + strlen(baseUrl) + strlen(path) + 1;
	char *url = malloc(urlSize);
	snprintf(url, urlSize, "%s%s%s", scheme, baseUrl, path);
	int ret = curl_easy_setopt(curl, CURLOPT_URL, url);
	free(url);
    return curl_error_to_gp(ret);
}

static int ccapi_curl_perform(CURL *curl, const struct Buffer *put_buffer, struct Buffer *buffer)
{
    long http_code;
    int ret;

    ret = GP_OK;

    buffer_init(buffer);
    CURL_CHECK(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_to_buffer));
    CURL_CHECK(curl_easy_setopt(curl, CURLOPT_WRITEDATA, buffer));
    CURL_CHECK(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0));
    CURL_CHECK(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0));
    CURL_CHECK(curl_easy_setopt(curl, CURLOPT_SSL_VERIFYSTATUS, 0));
    CURL_CHECK_GOTO_LOG(curl_easy_perform(curl), "could not perform HTTP request", cleanup);
    CURL_CHECK_GOTO(curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code), cleanup);
    
    if (http_code != 200)
    {
        fprintf(stderr, "Got error %ld\n", http_code);
        ret = GP_ERROR_CAMERA_ERROR;
    }

cleanup:
    return ret;
}

static int ccapi_curl_perform_json(CURL *curl, const json_object *put_object, json_object **ret_object)
{
    struct Buffer buffer;
    struct curl_slist *headers;
    enum json_tokener_error json_error;
    int ret = GP_OK;

    headers = curl_slist_append(NULL, "Accept: application/json");
    CURL_CHECK_GOTO(curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers), cleanup);

    GP_CHECK(ccapi_curl_perform(curl, NULL, &buffer))

    if (!buffer.data)
    {
        ret = GP_ERROR;
        goto cleanup;
    }

    *ret_object = json_tokener_parse_verbose(buffer.data, &json_error);
    if (!*ret_object)
    {
        fprintf(stderr, "Could not parse json response: %s\n", json_tokener_error_desc(json_error));
        ret = GP_ERROR_IO;
        goto cleanup;
    }

cleanup:
    curl_slist_free_all(headers);
    buffer_destroy(&buffer);
    return ret;
}

int ccapi_get(Camera *camera, const char *path, json_object **object)
{
	CURL *curl = camera->pl->curl;
	curl_easy_reset(curl);
	set_url(curl, camera, path);
    return ccapi_curl_perform_json(curl, NULL, object);
}

int ccapi_get_raw(Camera *camera, const char *path, struct Buffer *object)
{
	CURL *curl = camera->pl->curl;
	curl_easy_reset(curl);
	set_url(curl, camera, path);
    return ccapi_curl_perform(curl, NULL, object);
}

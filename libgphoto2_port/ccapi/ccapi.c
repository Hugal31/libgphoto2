/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* ccapi.c
 *
 * Copyright (c) 2006 Marcus Meissner <marcus@jet.franken.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include "config.h"
#include <gphoto2/gphoto2-port-library.h>

#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#include <errno.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#include <string.h>

#include <curl/curl.h>
#include <libgssdp/gssdp.h>
#include <libxml/parser.h>
#include <libxml/tree.h>
#include <libxml/xpath.h>
#include <libxml/xpathInternals.h>

#include <gphoto2/gphoto2-port.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port-log.h>

#include "libgphoto2_port/i18n.h"

#define CHECK(result) {int r=(result); if (r<0) return (r);}

struct SSDPData
{
	GMainLoop *main_loop;
	char *location;
};

struct Buffer
{
    char *data;
    size_t size;
};

static void
gp_gssdp_resource_available (G_GNUC_UNUSED GSSDPResourceBrowser *resource_browser,
                       const char                         *usn,
                       GList                              *locations,
					   struct SSDPData *data)
{
        GList *l;

		if (locations) {
		    data->location = strdup(locations->data);
			g_main_loop_quit(data->main_loop);
		}
}

static void gp_g_main_loop_timeout(void *loop)
{
	g_main_loop_quit(loop);
}

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

#define CURL_CHECK(op) { int ret = op; if (ret != CURLE_OK) return curl_error_to_gp(ret); }
#define CURL_CHECK_GOTO(op, label) { int curl_ret = op; if (curl_ret != CURLE_OK) { ret = curl_error_to_gp(curl_ret); goto label; } }

static int curl_error_to_gp(int curle)
{
	switch (curle)
	{
		case CURLE_OK: return GP_OK;
        case CURLE_OUT_OF_MEMORY: return GP_ERROR_NO_MEMORY;
		default: return GP_ERROR_IO;
	}
}

static int gp_detect_ccapi_via_upnp(GPPortInfoList *l, const char *location)
{
	int ret = GP_OK;
	CURL *curl;
	struct Buffer buffer;
	xmlDocPtr doc = NULL;
	xmlXPathContextPtr xpathCtx = NULL;
	xmlXPathObjectPtr xpathObject = NULL;

	buffer.data = NULL;
	buffer.size = 0;

	curl_global_init(CURL_GLOBAL_ALL);
	curl = curl_easy_init();
	CURL_CHECK_GOTO(curl_easy_setopt(curl, CURLOPT_URL, location), cleanup);
	CURL_CHECK_GOTO(curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &write_to_buffer), cleanup);
	CURL_CHECK_GOTO(curl_easy_setopt(curl, CURLOPT_WRITEDATA, &buffer), cleanup);
	CURL_CHECK_GOTO(curl_easy_perform(curl), cleanup);
	doc = xmlParseDoc((const xmlChar *)buffer.data);
	
	if (doc) {
	    xpathCtx = xmlXPathNewContext(doc);
		if (!xpathCtx) goto cleanup;

		xmlXPathRegisterNs(xpathCtx, "ns", "urn:schemas-canon-com:schema-upnp");
		xpathObject = xmlXPathEvalExpression("//ns:X_accessURL[1]", xpathCtx);
		if (!xpathObject) goto cleanup;

		xmlNodeSetPtr nodes = xpathObject->nodesetval;
		size_t size = (nodes) ? nodes->nodeNr : 0;
    
		xmlNodePtr cur;

		for(size_t i = 0; i < size; ++i) {
			if(nodes->nodeTab[i]->type == XML_ELEMENT_NODE) {
				cur = nodes->nodeTab[i];
				const char *url = xmlNodeGetContent(cur);
				if (!url) continue;
				const char *urlEnd = strstr(url, "://");
				if (!urlEnd) continue;
				urlEnd = strchr(urlEnd + 3, '/');
				if (!urlEnd) continue;
				GPPortInfo info;
				char *path = malloc(strlen("ccapi:") + (urlEnd - url) + 1);
				strcpy(path, "ccapi:");
				strncat(path, url, (urlEnd - url));
				gp_port_info_new (&info);
				gp_port_info_set_type (info, GP_PORT_CCAPI);
				gp_port_info_set_name (info, _("CCAPI Connection"));
				gp_port_info_set_path (info, path);
				gp_port_info_list_append (l, info); /* do not check return */
			}
		}
	}

cleanup:
	curl_easy_cleanup(curl);
	xmlXPathFreeObject(xpathObject);
	xmlXPathFreeContext(xpathCtx);
	xmlFreeDoc(doc);
	return ret;
}

static int gp_detect_ccapi_via_ssdp(GPPortInfoList *l)
{
	GSSDPClient *client;
	GSSDPResourceBrowser *resource_browser;
	GError *error;
	GMainLoop *main_loop;
	struct SSDPData ssdpCallbackData;

	error = NULL;
	client = gssdp_client_new_full (NULL,
									NULL,
									0,
									GSSDP_UDA_VERSION_1_0,
									&error);
	if (error) {
		g_printerr ("Error creating the GSSDP client: %s\n", error->message);
		g_error_free (error);

		return GP_ERROR_IO_INIT;
	}

	main_loop = g_main_loop_new (NULL, FALSE);
	resource_browser = gssdp_resource_browser_new (client,
												  "urn:schemas-canon-com:service:ICPO-CameraControlAPIService:1");
	ssdpCallbackData.main_loop = main_loop;
	ssdpCallbackData.location = NULL;
	g_signal_connect (resource_browser,
					"resource-available",
					G_CALLBACK (gp_gssdp_resource_available),
					&ssdpCallbackData);

	gssdp_resource_browser_set_active (resource_browser, TRUE);

	g_timeout_add_seconds_once(3, &gp_g_main_loop_timeout, main_loop);
	g_main_loop_run (main_loop);
	g_main_loop_unref (main_loop);

	g_object_unref (resource_browser);
	g_object_unref (client);

	if (ssdpCallbackData.location) {
		gp_detect_ccapi_via_upnp(l, ssdpCallbackData.location);
		free(ssdpCallbackData.location);
	}

	return GP_ERROR;
}

GPPortType
gp_port_library_type (void)
{
	return GP_PORT_CCAPI;
}

int
gp_port_library_list (GPPortInfoList *list)
{
    GPPortInfo info;

    gp_port_info_new (&info);
	gp_port_info_set_type (info, GP_PORT_CCAPI);
	gp_port_info_set_name (info, _("CCAPI Connection"));
	gp_port_info_set_path (info, "ccapi:");
	CHECK (gp_port_info_list_append (list, info));

	/* Generic matcher so you can pass any IP address */
	gp_port_info_new (&info);
	gp_port_info_set_type (info, GP_PORT_CCAPI);
	gp_port_info_set_name (info, "");
	gp_port_info_set_path (info, "^ccapi:");
	gp_port_info_list_append (list, info); /* do not check return */

	gp_detect_ccapi_via_ssdp(list);

    return GP_OK;
}

static int
gp_port_ccapi_open (GPPort *port)
{
	return GP_OK;
}

static int
gp_port_ccapi_close (GPPort *port)
{
	return GP_OK;
}

static int
gp_port_ccapi_update (GPPort *port)
{
	return GP_OK;
}

GPPortOperations *
gp_port_library_operations (void)
{
	GPPortOperations *ops;

	ops = calloc (1, sizeof (GPPortOperations));
	if (!ops)
		return NULL;

	ops->open   = gp_port_ccapi_open;
	ops->close  = gp_port_ccapi_close;
    ops->update = gp_port_ccapi_update;

    return ops;
}

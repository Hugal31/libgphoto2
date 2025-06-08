#include "ccapi.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include <gphoto2/gphoto2-library.h>

#include "requests.h"

int ccapi_list_content(Camera *camera, const char *folder, enum ContentType contentType, CameraList *list)
{
	int ret = GP_OK;
	json_object *object;

	// TODO DO not assume version
	const char *rootPath = "/ccapi/ver130/contents";
    if (strcmp(folder, "/") == 0)
        folder = folder + 1;

    size_t fullPathLength = strlen(rootPath) + strlen(folder);
	char *fullPath = malloc(strlen(rootPath) + strlen(folder) + 1);
	strcpy(fullPath, rootPath);
	strcat(fullPath, folder);
	if (fullPath[fullPathLength - 1] == '/')
	{
		--fullPathLength;
		fullPath[fullPathLength] = 0;
	}
	GP_CHECK_GOTO(ccapi_get(camera, fullPath, &object), cleanup);

	json_object *path;
	if (json_object_object_get_ex(object, "path", &path) == 0)
	{
		fprintf(stderr, "Could not find \"path\" in content get\n");
		ret = GP_ERROR;
		goto cleanup;
	}

	const size_t nEntries = json_object_array_length(path);
    const bool searchDirs = (contentType & CONTENT_TYPE_DIR) != 0;
	for (size_t i = 0; i < nEntries; ++i)
	{
		json_object *entry = json_object_array_get_idx(path, i);
		const char *entryPath = json_object_get_string(entry);
		if (entryPath)
		{
			if (!strstr(entryPath, fullPath) && entryPath[fullPathLength] != '/')
			{
				fprintf(stderr, "WARN: Expected %s to start with %s/\n", entryPath, fullPath);
			}
			else
			{
				const char *subPath = entryPath + strlen(fullPath) + 1;
                if ((strchr(subPath, '.') == NULL) == searchDirs)
    				gp_list_append(list, subPath, NULL);
			}
		}
	}

cleanup:
	free(fullPath);
	json_object_put(object);

	return GP_OK;
}
#include <curl/curl.h>

#include <gphoto2/gphoto2-camera.h>

#define GP_CHECK(op) { int ret = op; if (ret != GP_OK) return ret; }
#define GP_CHECK_GOTO(op, label) { ret = op; if (ret != GP_OK) goto label; }

struct _CameraPrivateLibrary
{
	CURL *curl;
};

enum ContentType
{
	CONTENT_TYPE_DIR = 1 << 0,
	CONTENT_TYPE_REGULAR = 1 << 1,
};

int ccapi_list_content(Camera *camera, const char *folder, enum ContentType contentType, CameraList *list);

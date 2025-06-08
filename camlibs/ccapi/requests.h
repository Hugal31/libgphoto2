#include <json-c/json.h>

#include <gphoto2/gphoto2-library.h>

struct Buffer
{
    char *data;
    size_t size;
};

int ccapi_get(Camera *camera, const char *path, json_object **object);
int ccapi_get_raw(Camera *camera, const char *path, struct Buffer *object);

void buffer_init(struct Buffer *buffer);
void buffer_destroy(struct Buffer *buffer);
int buffer_realloc(struct Buffer *buffer, size_t new_size);

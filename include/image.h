#ifndef PSPWAVE_IMAGE_H
#define PSPWAVE_IMAGE_H

#include <stdint.h>

/* unfortunately PSP does not support higher IMG size than 60x34 for XMB backgrounds */
#define PSPWAVE_IMAGE_WIDTH 60
#define PSPWAVE_IMAGE_HEIGHT 34

typedef struct {
	int width;
	int height;
	uint8_t *pixels;
} Image;

int image_load(const char *path, Image *image);
void image_free(Image *image);
int image_resize_cover(const Image *source, uint8_t *destination, int destination_width, int destination_height);

#endif

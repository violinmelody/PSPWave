#include "image.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint16_t read_u16(const uint8_t *p)
{
	return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t read_u32(const uint8_t *p)
{
	return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int32_t read_s32(const uint8_t *p)
{
	return (int32_t)read_u32(p);
}

static int clamp_int(int value, int minimum, int maximum)
{
	if (value < minimum) return minimum;
	if (value > maximum) return maximum;
	return value;
}

static uint8_t lerp_u8(uint8_t a, uint8_t b, float t)
{
	float value = (float)a + ((float)b - (float)a) * t;
	if (value < 0.0f) value = 0.0f;
	if (value > 255.0f) value = 255.0f;
	return (uint8_t)(value + 0.5f);
}

static void sample_bilinear(const Image *image, float x, float y, uint8_t *output)
{
	int x0 = (int)x;
	int y0 = (int)y;
	int x1 = x0 + 1;
	int y1 = y0 + 1;
	float tx = x - (float)x0;
	float ty = y - (float)y0;

	x0 = clamp_int(x0, 0, image->width - 1);
	y0 = clamp_int(y0, 0, image->height - 1);
	x1 = clamp_int(x1, 0, image->width - 1);
	y1 = clamp_int(y1, 0, image->height - 1);

	const uint8_t *p00 = &image->pixels[(y0 * image->width + x0) * 3];
	const uint8_t *p10 = &image->pixels[(y0 * image->width + x1) * 3];
	const uint8_t *p01 = &image->pixels[(y1 * image->width + x0) * 3];
	const uint8_t *p11 = &image->pixels[(y1 * image->width + x1) * 3];

	for (int channel = 0; channel < 3; ++channel)
	{
		uint8_t top = lerp_u8(p00[channel], p10[channel], tx);
		uint8_t bottom = lerp_u8(p01[channel], p11[channel], tx);
		output[channel] = lerp_u8(top, bottom, ty);
	}
}

int image_load(const char *path, Image *image)
{
	FILE *file;
	uint8_t header[54];
	int width, height, absolute_height, top_down, row_size;
	uint32_t data_offset;
	uint8_t *row = NULL;
	uint8_t *pixels = NULL;

	if (!path || !image) return -1;
	memset(image, 0, sizeof(*image));
	file = fopen(path, "rb");
	if (!file) return -1;

	if (fread(header, 1, sizeof(header), file) != sizeof(header))
	{
		fclose(file);
		return -1;
	}
	if (header[0] != 'B' || header[1] != 'M')
	{
		fclose(file);
		return -1;
	}

	data_offset = read_u32(&header[10]);
	width = read_s32(&header[18]);
	height = read_s32(&header[22]);
	if (width <= 0 || height == 0 || read_u16(&header[26]) != 1 || read_u16(&header[28]) != 24 || read_u32(&header[30]) != 0)
	{
		fclose(file);
		return -1;
	}

	top_down = height < 0;
	absolute_height = top_down ? -height : height;
	row_size = ((width * 3) + 3) & ~3;
	pixels = malloc((size_t)width * (size_t)absolute_height * 3);
	row = malloc((size_t)row_size);
	if (!pixels || !row)
	{
		free(pixels);
		free(row);
		fclose(file);
		return -1;
	}
	if (fseek(file, (long)data_offset, SEEK_SET) != 0)
	{
		free(pixels);
		free(row);
		fclose(file);
		return -1;
	}

	for (int stored_y = 0; stored_y < absolute_height; ++stored_y)
	{
		int logical_y;
		if (fread(row, 1, (size_t)row_size, file) != (size_t)row_size)
		{
			free(pixels);
			free(row);
			fclose(file);
			return -1;
		}
		logical_y = top_down ? stored_y : absolute_height - 1 - stored_y;
		for (int x = 0; x < width; ++x)
		{
			const uint8_t *source = &row[x * 3];
			uint8_t *destination = &pixels[(logical_y * width + x) * 3];
			destination[0] = source[2];
			destination[1] = source[1];
			destination[2] = source[0];
		}
	}

	free(row);
	fclose(file);
	image->width = width;
	image->height = absolute_height;
	image->pixels = pixels;
	return 0;
}

void image_free(Image *image)
{
	if (!image) return;
	free(image->pixels);
	image->pixels = NULL;
	image->width = 0;
	image->height = 0;
}

int image_resize_cover(const Image *source, uint8_t *destination, int destination_width, int destination_height)
{
	float scale_x, scale_y, scale, visible_width, visible_height, source_left, source_top;
	if (!source || !source->pixels || !destination || source->width <= 0 || source->height <= 0 || destination_width <= 0 || destination_height <= 0) return -1;

	scale_x = (float)destination_width / (float)source->width;
	scale_y = (float)destination_height / (float)source->height;
	scale = scale_x > scale_y ? scale_x : scale_y;
	visible_width = (float)destination_width / scale;
	visible_height = (float)destination_height / scale;
	source_left = ((float)source->width - visible_width) * 0.5f;
	source_top = ((float)source->height - visible_height) * 0.5f;

	for (int y = 0; y < destination_height; ++y)
		for (int x = 0; x < destination_width; ++x)
		{
			float source_x = source_left + ((float)x + 0.5f) / scale - 0.5f;
			float source_y = source_top + ((float)y + 0.5f) / scale - 0.5f;
			uint8_t *pixel = &destination[(y * destination_width + x) * 3];
			sample_bilinear(source, source_x, source_y, pixel);
		}
	return 0;
}

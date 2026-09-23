#include "wavegen.h"
#include "image.h"
#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

#define RECORD_SIZE 6176
#define PIXEL_OFFSET 54
#define WIDTH 60
#define HEIGHT 34
#define FIRST_COUNT 12
#define SECOND_COUNT 22
#define FIRST_SIZE ((SceOff)FIRST_COUNT * RECORD_SIZE)
#define SECOND_SIZE ((SceOff)SECOND_COUNT * RECORD_SIZE)
#define TMP1 PSPWAVE_PLUGIN_DIR "/1.tmp"
#define TMP2 PSPWAVE_PLUGIN_DIR "/2.tmp"

static WavegenStatus g_status;

static void set_status(WavegenStatus *s, int err, const char *msg)
{
	s->last_error = err;
	strncpy(s->message, msg, sizeof(s->message) - 1);
	s->message[sizeof(s->message) - 1] = 0;
	g_status = *s;
}

static int validate(const char *path, SceOff expected)
{
	SceIoStat st;
	unsigned char sig[2];
	SceUID f;
	if (sceIoGetstat(path, &st) < 0 || st.st_size < expected) return -1;
	f = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (f < 0) return f;
	int n = sceIoRead(f, sig, 2);
	sceIoClose(f);
	return n == 2 && sig[0] == 'B' && sig[1] == 'M' ? 0 : -2;
}

static unsigned char lerp_u8(unsigned char a, unsigned char b, float t)
{
	float value = (float)a + ((float)b - (float)a) * t;
	if (value < 0.0f) value = 0.0f;
	if (value > 255.0f) value = 255.0f;
	return (unsigned char)(value + 0.5f);
}

static float clamp01(float v)
{
	if (v < 0.0f) return 0.0f;
	if (v > 1.0f) return 1.0f;
	return v;
}

static float gradient_position(GradientMode mode, int x, int y)
{
	float nx = (float)x / (float)(WIDTH - 1);
	float ny = (float)y / (float)(HEIGHT - 1);
	float cx = nx - 0.5f;
	float cy = ny - 0.5f;
	float t;

	switch(mode)
	{
		case GRADIENT_LINEAR_LEFT_RIGHT:
			return nx;
		case GRADIENT_LINEAR_RIGHT_LEFT:
			return 1.0f - nx;
		case GRADIENT_LINEAR_TOP_BOTTOM:
			return ny;
		case GRADIENT_LINEAR_BOTTOM_TOP:
			return 1.0f - ny;
		case GRADIENT_LINEAR_TL_BR:
			return (nx + ny) * 0.5f;
		case GRADIENT_LINEAR_TR_BL:
			return ((1.0f - nx) + ny) * 0.5f;
		case GRADIENT_RADIAL:
			t = sqrtf(cx * cx + cy * cy) / 0.70710678f;
			return clamp01(t);
		case GRADIENT_ANGLE:
			t = (atan2f(cy, cx) + 3.14159265f) / 6.28318531f;
			return clamp01(t);
		case GRADIENT_REFLECTED_HORIZONTAL:
			return clamp01(fabsf(cx) * 2.0f);
		case GRADIENT_REFLECTED_VERTICAL:
			return clamp01(fabsf(cy) * 2.0f);
		case GRADIENT_DIAMOND:
			return clamp01((fabsf(cx) + fabsf(cy)) * 1.0f);
		default:
			return nx;
	}
}

static Rgb gradient_colour(const WaveSlot *slot, float t)
{
	Rgb a = slot->colour[0];
	Rgb b = slot->count >= 2 ? slot->colour[1] : a;
	Rgb out;
	t = clamp01(t);

	if (slot->count >= 3)
	{
		Rgb c = slot->colour[2];
		if (t <= 0.5f)
		{
			float local = t * 2.0f;
			out.r = lerp_u8(a.r, b.r, local);
			out.g = lerp_u8(a.g, b.g, local);
			out.b = lerp_u8(a.b, b.b, local);
		}
		else
		{
			float local = (t - 0.5f) * 2.0f;
			out.r = lerp_u8(b.r, c.r, local);
			out.g = lerp_u8(b.g, c.g, local);
			out.b = lerp_u8(b.b, c.b, local);
		}
		return out;
	}

	out.r = lerp_u8(a.r, b.r, t);
	out.g = lerp_u8(a.g, b.g, t);
	out.b = lerp_u8(a.b, b.b, t);
	return out;
}

Rgb wavegen_preview_sample(const WaveSlot *slot, float u, float v)
{
	int x = (int)(clamp01(u) * (float)(WIDTH - 1) + 0.5f);
	int y = (int)(clamp01(v) * (float)(HEIGHT - 1) + 0.5f);
	return gradient_colour(slot, gradient_position(slot->gradient, x, y));
}

static void put_u16(unsigned char *p, unsigned int value)
{
	p[0] = (unsigned char)(value & 0xff);
	p[1] = (unsigned char)((value >> 8) & 0xff);
}

static void put_u32(unsigned char *p, unsigned int value)
{
	p[0] = (unsigned char)(value & 0xff);
	p[1] = (unsigned char)((value >> 8) & 0xff);
	p[2] = (unsigned char)((value >> 16) & 0xff);
	p[3] = (unsigned char)((value >> 24) & 0xff);
}

static int storage_index_to_xmb_index(int storage_index)
{
	return (storage_index + 22) % 34;
}

static int write_image_record(SceUID f, const WaveSlot *slot)
{
	Image source;
	unsigned char *rgb = NULL;
	unsigned char *bmp = NULL;
	int width = PSPWAVE_IMAGE_WIDTH;
	int height = PSPWAVE_IMAGE_HEIGHT;
	int row_size = ((width * 3) + 3) & ~3;
	int pixel_size = row_size * height;
	int bmp_size = PIXEL_OFFSET + pixel_size;
	int record_size = (bmp_size + 3) & ~3;
	int written;

	if (!slot || slot->image_path[0] == '\0') return -1;
	memset(&source, 0, sizeof(source));
	if (image_load(slot->image_path, &source) < 0) return -1;

	rgb = malloc((size_t)width * (size_t)height * 3);
	bmp = calloc(1, (size_t)record_size);
	if (!rgb || !bmp)
	{
		free(rgb);
		free(bmp);
		image_free(&source);
		return -1;
	}

	if (image_resize_cover(&source, rgb, width, height) < 0)
	{
		free(rgb);
		free(bmp);
		image_free(&source);
		return -1;
	}
	image_free(&source);

	bmp[0] = 'B';
	bmp[1] = 'M';
	put_u32(&bmp[2], record_size);
	put_u32(&bmp[10], PIXEL_OFFSET);
	put_u32(&bmp[14], 40);
	put_u32(&bmp[18], width);
	put_u32(&bmp[22], height);
	put_u16(&bmp[26], 1);
	put_u16(&bmp[28], 24);
	put_u32(&bmp[34], pixel_size);

	for (int stored_y = 0; stored_y < height; ++stored_y)
	{
		int source_y = height - 1 - stored_y;
		unsigned char *destination = &bmp[PIXEL_OFFSET + stored_y * row_size];
		const unsigned char *source_row = &rgb[source_y * width * 3];
		for (int x = 0; x < width; ++x)
		{
			destination[x * 3 + 0] = source_row[x * 3 + 2];
			destination[x * 3 + 1] = source_row[x * 3 + 1];
			destination[x * 3 + 2] = source_row[x * 3 + 0];
		}
	}

	written = sceIoWrite(f, bmp, record_size);
	free(rgb);
	free(bmp);
	return written == record_size ? 0 : -1;
}

static int write_pack(const char *path, const WaveConfig *cfg, int first, int count)
{
	SceUID f = sceIoOpen(path, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if (f < 0) return f;

	for (int i = 0; i < count; ++i)
	{
		unsigned char bmp[RECORD_SIZE];
		int offset = PIXEL_OFFSET;
		
		int storage_index = first + i;
		int wave_index = storage_index_to_xmb_index(storage_index);
		const WaveSlot *slot = &cfg->slot[wave_index];

		if (slot->mode == WAVE_MODE_IMAGE)
		{
			if (write_image_record(f, slot) < 0)
			{
				sceIoClose(f);
				sceIoRemove(path);
				return -5;
			}
			continue;
		}

		memset(bmp, 0, sizeof(bmp));
		bmp[0] = 'B';
		bmp[1] = 'M';
		put_u32(&bmp[2], RECORD_SIZE);
		put_u32(&bmp[10], PIXEL_OFFSET);
		put_u32(&bmp[14], 40);
		put_u32(&bmp[18], WIDTH);
		put_u32(&bmp[22], HEIGHT);
		put_u16(&bmp[26], 1);
		put_u16(&bmp[28], 24);
		put_u32(&bmp[34], WIDTH * HEIGHT * 3);

		for (int y = 0; y < HEIGHT; ++y)
		{
			for (int x = 0; x < WIDTH; ++x)
			{
				Rgb c = gradient_colour(slot, gradient_position(slot->gradient, x, HEIGHT - 1 - y));
				bmp[offset++] = c.b;
				bmp[offset++] = c.g;
				bmp[offset++] = c.r;
			}
		}
		if (sceIoWrite(f, bmp, sizeof(bmp)) != (int)sizeof(bmp))
		{
			sceIoClose(f);
			sceIoRemove(path);
			return -3;
		}
	}
	sceIoClose(f);
	return 0;
}

static int commit(const char *tmp, const char *dst)
{
	sceIoRemove(dst);
	int r = sceIoRename(tmp, dst);
	if (r < 0) sceIoRemove(tmp);
	return r;
}

void wavegen_get_status(WavegenStatus *s)
{
	memset(s, 0, sizeof(*s));
	SceIoStat st;
	s->resource1_ready = validate(PSPWAVE_RESOURCE1_PATH, FIRST_SIZE) == 0;
	s->resource2_ready = validate(PSPWAVE_RESOURCE2_PATH, SECOND_SIZE) == 0;
	if (sceIoGetstat(PSPWAVE_RESOURCE1_PATH, &st) == 0) s->resource1_size = st.st_size;
	if (sceIoGetstat(PSPWAVE_RESOURCE2_PATH, &st) == 0) s->resource2_size = st.st_size;
	s->initialized = s->resource1_ready && s->resource2_ready;
	if (s->initialized) strcpy(s->message, "RESOURCE PACKS READY");
	else strcpy(s->message, "RESOURCE PACKS NOT GENERATED");
	g_status = *s;
}

int wavegen_generate(const WaveConfig *cfg, WavegenStatus *s)
{
	WavegenStatus local_status;
	if (!s) s = &local_status;
	memset(s, 0, sizeof(*s));
	sceIoMkdir("ms0:/SEPLUGINS", 0777);
	sceIoMkdir(PSPWAVE_PLUGIN_DIR, 0777);
	sceIoRemove(TMP1);
	sceIoRemove(TMP2);

	int r = write_pack(TMP1, cfg, 0, FIRST_COUNT);
	if (r < 0)
	{
		set_status(s, r, "FAILED TO BUILD FIRST RESOURCE PACK");
		return r;
	}
	r = write_pack(TMP2, cfg, FIRST_COUNT, SECOND_COUNT);
	if (r < 0)
	{
		sceIoRemove(TMP1);
		set_status(s, r, "FAILED TO BUILD SECOND RESOURCE PACK");
		return r;
	}
	if (validate(TMP1, FIRST_SIZE) < 0 || validate(TMP2, SECOND_SIZE) < 0)
	{
		sceIoRemove(TMP1);
		sceIoRemove(TMP2);
		set_status(s, -4, "GENERATED RESOURCE VALIDATION FAILED");
		return -4;
	}
	r = commit(TMP1, PSPWAVE_RESOURCE1_PATH);
	if (r < 0)
	{
		sceIoRemove(TMP2);
		set_status(s, r, "FAILED TO INSTALL FIRST RESOURCE PACK");
		return r;
	}
	r = commit(TMP2, PSPWAVE_RESOURCE2_PATH);
	if (r < 0)
	{
		set_status(s, r, "FAILED TO INSTALL SECOND RESOURCE PACK");
		return r;
	}
	wavegen_get_status(s);
	strcpy(s->message, "SAVED - RESTART VSH TO APPLY");
	g_status = *s;
	return 0;
}

int wavegen_initialize(const WaveConfig *cfg, WavegenStatus *s)
{
	wavegen_get_status(s);
	if (s->initialized) return 0;
	return wavegen_generate(cfg, s);
}

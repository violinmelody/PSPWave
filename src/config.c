#include "config.h"

#include <pspiofilemgr.h>
#include <stdio.h>
#include <string.h>

static int hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int parse_color(const char *text, Rgb *color)
{
	int v[6];
	if (*text == '#') ++text;
	for (int i = 0; i < 6; ++i) {
		v[i] = hex_value(text[i]);
		if (v[i] < 0) return -1;
	}
	color->r = (uint8_t)((v[0] << 4) | v[1]);
	color->g = (uint8_t)((v[2] << 4) | v[3]);
	color->b = (uint8_t)((v[4] << 4) | v[5]);
	return 0;
}

static Rgb rgb(unsigned int value)
{
	Rgb c = {(uint8_t)(value >> 16), (uint8_t)(value >> 8), (uint8_t)value};
	return c;
}

static const char *gradient_keys[GRADIENT_MODE_COUNT] = {
	"LINEAR_LR", "LINEAR_RL", "LINEAR_TB", "LINEAR_BT", "LINEAR_TL_BR", "LINEAR_TR_BL",
	"RADIAL", "ANGLE", "REFLECTED_H", "REFLECTED_V", "DIAMOND"
};

static const char *gradient_names[GRADIENT_MODE_COUNT] = {
	"LINEAR LEFT TO RIGHT", "LINEAR RIGHT TO LEFT", "LINEAR TOP TO BOTTOM", "LINEAR BOTTOM TO TOP",
	"LINEAR TOP LEFT TO BOTTOM RIGHT", "LINEAR TOP RIGHT TO BOTTOM LEFT", "RADIAL", "ANGLE",
	"REFLECTED HORIZONTAL", "REFLECTED VERTICAL", "DIAMOND"
};

const char *config_gradient_name(GradientMode mode)
{
	if (mode < 0 || mode >= GRADIENT_MODE_COUNT) return "LINEAR LEFT TO RIGHT";
	return gradient_names[mode];
}

const char *config_gradient_key(GradientMode mode)
{
	if (mode < 0 || mode >= GRADIENT_MODE_COUNT) return "LINEAR_LR";
	return gradient_keys[mode];
}

int config_gradient_from_key(const char *key, GradientMode *mode)
{
	for (int i = 0; i < GRADIENT_MODE_COUNT; ++i)
	{
		if (strcmp(key, gradient_keys[i]) == 0)
		{
			*mode = (GradientMode)i;
			return 0;
		}
	}
	return -1;
}

void config_defaults(WaveConfig *cfg)
{
	static const struct {
		unsigned int c1, c2, c3;
		unsigned char count;
		GradientMode gradient;
	} defaults[PSPWAVE_SLOTS] = {
		{0xDCEEFF,0xAFCBF2,0,2,GRADIENT_LINEAR_LEFT_RIGHT},
		{0xEEE4FF,0xC9B8E8,0,2,GRADIENT_LINEAR_TL_BR},
		{0xFFE0E8,0xE9AFC2,0,2,GRADIENT_RADIAL},
		{0xFFE7D2,0xE9B68F,0,2,GRADIENT_LINEAR_TR_BL},
		{0xDCF5E7,0xA9D5BE,0,2,GRADIENT_LINEAR_TOP_BOTTOM},
		{0xD9F5F2,0x9FD1D5,0,2,GRADIENT_DIAMOND},
		{0xE1E6FF,0xAEBBE9,0,2,GRADIENT_REFLECTED_HORIZONTAL},
		{0xFFF1D2,0xDDBF78,0,2,GRADIENT_LINEAR_BOTTOM_TOP},
		{0xF0DDF5,0xC59DCE,0,2,GRADIENT_REFLECTED_VERTICAL},
		{0xE3EED6,0xAFC58F,0,2,GRADIENT_LINEAR_TL_BR},
		{0xFFDDE4,0xD99AAA,0,2,GRADIENT_RADIAL},
		{0xE1F1FF,0xB5D6F0,0xF5FAFF,3,GRADIENT_ANGLE},
		{0xBFC7FF,0x7F93D6,0,2,GRADIENT_LINEAR_RIGHT_LEFT},
		{0xF5C8C4,0xD9907F,0,2,GRADIENT_LINEAR_TR_BL},
		{0xBCE8DE,0x79B9C8,0,2,GRADIENT_LINEAR_TOP_BOTTOM},
		{0xDFC3F4,0x9E8BC8,0,2,GRADIENT_REFLECTED_HORIZONTAL},
		{0xF4C9DB,0xC59AD8,0,2,GRADIENT_RADIAL},
		{0xC5E7D2,0x86B8A0,0xB8D8F0,3,GRADIENT_DIAMOND},
		{0xE8C98F,0xB88A3D,0xF6E4B8,3,GRADIENT_REFLECTED_VERTICAL},
		{0xE7C4A3,0xB76E79,0xF2D9C9,3,GRADIENT_LINEAR_TL_BR},
		{0xD9C48E,0xA77B35,0,2,GRADIENT_LINEAR_LEFT_RIGHT},
		{0xD5A79D,0xA96373,0xE8C7B8,3,GRADIENT_RADIAL},
		{0xAFCFEA,0x668FAE,0,2,GRADIENT_LINEAR_BOTTOM_TOP},
		{0xE5B8D3,0xA778B4,0,2,GRADIENT_REFLECTED_HORIZONTAL},
		{0xA9D9C7,0x5E9E8A,0xD3E8BE,3,GRADIENT_DIAMOND},
		{0xE9C39B,0xC17E76,0,2,GRADIENT_LINEAR_TR_BL},
		{0xAEB8E8,0x76669E,0xD9BFD8,3,GRADIENT_ANGLE},
		{0x9ED5D0,0x5A879D,0,2,GRADIENT_LINEAR_RIGHT_LEFT},
		{0xE9C49D,0xB96E88,0xB09BD0,3,GRADIENT_REFLECTED_VERTICAL},
		{0xB8D6B7,0x728D68,0,2,GRADIENT_LINEAR_TOP_BOTTOM},
		{0xB6D4EA,0x786AAB,0,2,GRADIENT_LINEAR_TL_BR},
		{0xE2AFC2,0x8E668B,0,2,GRADIENT_RADIAL},
		{0x9CCDBB,0x647FB0,0xC39BC6,3,GRADIENT_ANGLE},
		{0xC7A6D8,0x7098B5,0x9DC6A8,3,GRADIENT_DIAMOND}
	};

	memset(cfg, 0, sizeof(*cfg));
	for (int i = 0; i < PSPWAVE_SLOTS; ++i)
	{
		cfg->slot[i].count = defaults[i].count;
		cfg->slot[i].gradient = defaults[i].gradient;
		cfg->slot[i].mode = WAVE_MODE_GRADIENT;
		cfg->slot[i].image_path[0] = '\0';
		cfg->slot[i].color[0] = rgb(defaults[i].c1);
		cfg->slot[i].color[1] = rgb(defaults[i].count >= 2 ? defaults[i].c2 : defaults[i].c1);
		cfg->slot[i].color[2] = rgb(defaults[i].count >= 3 ? defaults[i].c3 : defaults[i].c1);
	}
}

int config_load(WaveConfig *cfg, const char *path)
{
	SceUID fd = sceIoOpen(path, PSP_O_RDONLY, 0);
	char buffer[16384];
	int bytes, loaded = 0;
	char *line;

	if (fd < 0) return fd;
	bytes = sceIoRead(fd, buffer, sizeof(buffer) - 1);
	sceIoClose(fd);
	if (bytes <= 0) return -1;
	buffer[bytes] = '\0';
	line = buffer;

	while (line && *line)
	{
		char *end = strchr(line, '\n');
		int index = 0;
		char gradient[32];
		if (end) *end = '\0';
		if (sscanf(line, "%d %31s", &index, gradient) == 2 && index >= 1 && index <= PSPWAVE_SLOTS)
		{
			if (strcmp(gradient, "IMAGE") == 0)
			{
				char *cursor = strchr(line, '=');
				if (!cursor) return -1;
				++cursor;
				while (*cursor == ' ' || *cursor == '\t') ++cursor;
				cfg->slot[index - 1].mode = WAVE_MODE_IMAGE;
				strncpy(cfg->slot[index - 1].image_path, cursor, sizeof(cfg->slot[index - 1].image_path) - 1);
				cfg->slot[index - 1].image_path[sizeof(cfg->slot[index - 1].image_path) - 1] = '\0';
				++loaded;
			}
			else
			{
				GradientMode mode;
				char *cursor;
				int count = 0;
				if (config_gradient_from_key(gradient, &mode) < 0) return -1;
				cursor = strchr(line, '#');
				while (cursor && count < PSPWAVE_MAX_COLORS)
				{
					if (parse_color(cursor, &cfg->slot[index - 1].color[count]) == 0) ++count;
					cursor = strchr(cursor + 1, '#');
				}
				if (count > 0)
				{
					cfg->slot[index - 1].count = count;
					cfg->slot[index - 1].gradient = mode;
					cfg->slot[index - 1].mode = WAVE_MODE_GRADIENT;
					cfg->slot[index - 1].image_path[0] = '\0';
					++loaded;
				}
			}
		}
		line = end ? end + 1 : NULL;
	}
	return loaded == PSPWAVE_SLOTS ? 0 : -1;
}

int config_save(const WaveConfig *cfg, const char *path)
{
	char temporary[128], line[384];
	SceUID fd;
	int result;

	snprintf(temporary, sizeof(temporary), "%s.tmp", path);
	sceIoRemove(temporary);
	fd = sceIoOpen(temporary, PSP_O_WRONLY | PSP_O_CREAT | PSP_O_TRUNC, 0777);
	if (fd < 0) return fd;

	for (int i = 0; i < PSPWAVE_SLOTS; ++i)
	{
		int count = cfg->slot[i].count;
		int length;
		if (count < 1 || count > PSPWAVE_MAX_COLORS)
		{
			sceIoClose(fd);
			sceIoRemove(temporary);
			return -1;
		}
		if (cfg->slot[i].mode == WAVE_MODE_IMAGE)
		{
			length = snprintf(line, sizeof(line), "%d IMAGE = %s", i + 1, cfg->slot[i].image_path);
		}
		else
		{
			length = sprintf(line, "%d %s =", i + 1, config_gradient_key(cfg->slot[i].gradient));
			for (int k = 0; k < count; ++k)
			{
				Rgb c = cfg->slot[i].color[k];
				length += sprintf(line + length, " #%02X%02X%02X", c.r, c.g, c.b);
			}
		}
		line[length++] = '\n';
		if (sceIoWrite(fd, line, length) != length)
		{
			sceIoClose(fd);
			sceIoRemove(temporary);
			return -1;
		}
	}

	sceIoClose(fd);
	sceIoRemove(path);
	result = sceIoRename(temporary, path);
	if (result < 0) sceIoRemove(temporary);
	return result;
}

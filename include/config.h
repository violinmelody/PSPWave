#ifndef PSPWAVE_CONFIG_H
#define PSPWAVE_CONFIG_H

#include <stdint.h>

#define PSPWAVE_SLOTS 34
#define PSPWAVE_MAX_COLOURS 3
#define PSPWAVE_PLUGIN_DIR "ms0:/SEPLUGINS/PSPWave"
#define PSPWAVE_CONFIG_PATH PSPWAVE_PLUGIN_DIR "/PSPWave.txt"
#define PSPWAVE_RESOURCE1_PATH PSPWAVE_PLUGIN_DIR "/1.bmp"
#define PSPWAVE_RESOURCE2_PATH PSPWAVE_PLUGIN_DIR "/2.bmp"

typedef struct {
	uint8_t r;
	uint8_t g;
	uint8_t b;
} Rgb;

typedef enum {
	GRADIENT_LINEAR_LEFT_RIGHT = 0,
	GRADIENT_LINEAR_RIGHT_LEFT,
	GRADIENT_LINEAR_TOP_BOTTOM,
	GRADIENT_LINEAR_BOTTOM_TOP,
	GRADIENT_LINEAR_TL_BR,
	GRADIENT_LINEAR_TR_BL,
	GRADIENT_RADIAL,
	GRADIENT_ANGLE,
	GRADIENT_REFLECTED_HORIZONTAL,
	GRADIENT_REFLECTED_VERTICAL,
	GRADIENT_DIAMOND,
	GRADIENT_MODE_COUNT
} GradientMode;

typedef enum {
	WAVE_MODE_GRADIENT = 0,
	WAVE_MODE_IMAGE
} WaveMode;

typedef struct {
	Rgb colour[PSPWAVE_MAX_COLOURS];
	Rgb menu_colour;
	int count;
	GradientMode gradient;
	WaveMode mode;
	char image_path[256];
} WaveSlot;

typedef struct {
	WaveSlot slot[PSPWAVE_SLOTS];
} WaveConfig;

void config_defaults(WaveConfig *cfg);
int config_load(WaveConfig *cfg, const char *path);
int config_save(const WaveConfig *cfg, const char *path);
const char *config_gradient_name(GradientMode mode);
const char *config_gradient_key(GradientMode mode);
int config_gradient_from_key(const char *key, GradientMode *mode);

#endif

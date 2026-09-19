#ifndef PSPWAVE_WAVEGEN_H
#define PSPWAVE_WAVEGEN_H

#include "config.h"

#define PSPWAVE_STATUS_TEXT 160

typedef struct {
	int initialized;
	int resource1_ready;
	int resource2_ready;
	long long resource1_size;
	long long resource2_size;
	int last_error;
	char message[PSPWAVE_STATUS_TEXT];
} WavegenStatus;

int wavegen_initialize(const WaveConfig *cfg, WavegenStatus *status);
int wavegen_generate(const WaveConfig *cfg, WavegenStatus *status);
void wavegen_get_status(WavegenStatus *status);
Rgb wavegen_preview_sample(const WaveSlot *slot, float u, float v);

#endif

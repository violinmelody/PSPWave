#ifndef PSPWAVE_THEMES_H
#define PSPWAVE_THEMES_H

#include "config.h"

#define PSPWAVE_THEMES_DIR PSPWAVE_PLUGIN_DIR "/Themes"
#define PSPWAVE_MAX_THEMES 32
#define PSPWAVE_THEME_NAME 32

typedef struct {
    char name[PSPWAVE_THEME_NAME];
    char path[160];
} ThemeEntry;

typedef struct {
    ThemeEntry item[PSPWAVE_MAX_THEMES];
    int count;
} ThemeList;

int themes_init(void);
int themes_scan(ThemeList *list);
int themes_load(const ThemeEntry *entry, WaveConfig *cfg);
int themes_save(const char *name, const WaveConfig *cfg);
int themes_delete(const ThemeEntry *entry);
int themes_activate(const char *name, const WaveConfig *cfg);
int themes_active_name(char *name, int size);
int themes_sanitize_name(const char *input, char *output, int size);

#endif

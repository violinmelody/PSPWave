#include <pspkernel.h>
#include <pspmodulemgr.h>
#include <pspiofilemgr.h>
#include <string.h>

PSP_MODULE_INFO("PSPWave", 0x1000, 1, 0);
PSP_MAIN_THREAD_ATTR(0);

#define PSPWAVE_FW_661 0x06060110
#define PSPWAVE_SCAN_DELAY_US 100000
#define PSPWAVE_MAX_MODULES 100
#define PSPWAVE_RECORD_SIZE 6176
#define PSPWAVE_RESOURCE_1 "ms0:/SEPLUGINS/PSPWave/1.bmp"
#define PSPWAVE_RESOURCE_2 "ms0:/SEPLUGINS/PSPWave/2.bmp"
#define PSPWAVE_CONFIG "ms0:/SEPLUGINS/PSPWave/PSPWave.txt"
#define PSPWAVE_THEME_PALETTE_OFFSET 0x45CD0
#define PSPWAVE_MENU_COLOUR_COUNT 34


typedef struct PspWaveThemeColour {
	float r;
	float g;
	float b;
} PspWaveThemeColour;

typedef struct PspWaveRgb {
	unsigned char r;
	unsigned char g;
	unsigned char b;
} PspWaveRgb;

static int hex_value(char c)
{
	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'a' && c <= 'f') return c - 'a' + 10;
	if (c >= 'A' && c <= 'F') return c - 'A' + 10;
	return -1;
}

static int parse_hex_colour(const char *text, PspWaveRgb *colour)
{
	int v[6];
	if (*text == '#') ++text;
	for (int i = 0; i < 6; ++i)
	{
		v[i] = hex_value(text[i]);
		if (v[i] < 0) return 0;
	}
	colour->r = (unsigned char)((v[0] << 4) | v[1]);
	colour->g = (unsigned char)((v[2] << 4) | v[3]);
	colour->b = (unsigned char)((v[4] << 4) | v[5]);
	return 1;
}

static int load_menu_colours(PspWaveRgb colours[PSPWAVE_MENU_COLOUR_COUNT])
{
	SceUID fd;
	char buffer[16384];
	char *line;
	int bytes;
	int found = 0;

	fd = sceIoOpen(PSPWAVE_CONFIG, PSP_O_RDONLY, 0);
	if (fd < 0) return 0;
	bytes = sceIoRead(fd, buffer, sizeof(buffer) - 1);
	sceIoClose(fd);
	if (bytes <= 0) return 0;
	buffer[bytes] = '\0';
	line = buffer;

	while (line && *line)
	{
		char *end = strchr(line, '\n');
		if (end) *end = '\0';
		if (strncmp(line, "MENU ", 5) == 0)
		{
			const char *cursor = line + 5;
			int index = 0;
			while (*cursor >= '0' && *cursor <= '9')
			{
				index = index * 10 + (*cursor - '0');
				++cursor;
			}
			while (*cursor == ' ' || *cursor == '\t') ++cursor;
			if (index >= 1 && index <= PSPWAVE_MENU_COLOUR_COUNT && parse_hex_colour(cursor, &colours[index - 1])) ++found;
		}
		line = end ? end + 1 : NULL;
	}
	return found == PSPWAVE_MENU_COLOUR_COUNT;
}

static int float_in_unit_range(float value)
{
	return value >= 0.0f && value <= 1.05f;
}

static int patch_menu_palette(void)
{
	SceUID module_ids[PSPWAVE_MAX_MODULES];
	PspWaveRgb menu[PSPWAVE_MENU_COLOUR_COUNT];
	int module_count = 0;

	if (!load_menu_colours(menu)) return 0;
	if (sceKernelGetModuleIdList(module_ids, sizeof(module_ids), &module_count) < 0) return 0;
	if (module_count > PSPWAVE_MAX_MODULES) module_count = PSPWAVE_MAX_MODULES;

	for (int i = 0; i < module_count; ++i)
	{
		SceKernelModuleInfo info;
		PspWaveThemeColour *colours;
		unsigned int image_size;
		int plausible = 0;

		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelQueryModuleInfo(module_ids[i], &info) < 0) continue;
		if (!strstr(info.name, "vsh")) continue;
		image_size = info.text_size + info.data_size;
		if (PSPWAVE_THEME_PALETTE_OFFSET + sizeof(PspWaveThemeColour) * PSPWAVE_MENU_COLOUR_COUNT > image_size) continue;

		colours = (PspWaveThemeColour *)((unsigned char *)info.text_addr + PSPWAVE_THEME_PALETTE_OFFSET);
		for (int k = 0; k < 6; ++k)
		{
			if (float_in_unit_range(colours[k].r) && float_in_unit_range(colours[k].g) && float_in_unit_range(colours[k].b)) ++plausible;
		}
		if (plausible != 6) continue;

		for (int k = 0; k < PSPWAVE_MENU_COLOUR_COUNT; ++k)
		{
			colours[k].r = menu[k].r / 255.0f;
			colours[k].g = menu[k].g / 255.0f;
			colours[k].b = menu[k].b / 255.0f;
		}
		sceKernelDcacheWritebackInvalidateRange(colours, sizeof(PspWaveThemeColour) * PSPWAVE_MENU_COLOUR_COUNT);
		return 1;
	}
	return 0;
}

static int valid_resource(const char *path, SceOff expected_size)
{
	SceIoStat stat;
	SceUID fd;
	unsigned char signature[2];

	if (sceIoGetstat(path, &stat) < 0 || stat.st_size < expected_size) return 0;
	fd = sceIoOpen(path, PSP_O_RDONLY, 0);
	if (fd < 0) return 0;
	if (sceIoRead(fd, signature, sizeof(signature)) != (int)sizeof(signature))
	{
		sceIoClose(fd);
		return 0;
	}
	sceIoClose(fd);
	return signature[0] == 'B' && signature[1] == 'M';
}

static int resources_ready(void)
{
	return valid_resource(PSPWAVE_RESOURCE_1, (SceOff)12 * PSPWAVE_RECORD_SIZE) && valid_resource(PSPWAVE_RESOURCE_2, (SceOff)22 * PSPWAVE_RECORD_SIZE);
}

static int replace_path(char *address, const char *source, const char *target, unsigned int field_size)
{
	unsigned int target_len = (unsigned int)strlen(target);
	if (strncmp(address, source, field_size) != 0 || target_len >= field_size) return 0;
	memset(address, 0, field_size);
	memcpy(address, target, target_len);
	sceKernelDcacheWritebackInvalidateRange(address, field_size);
	return 1;
}

static int patch_background_paths(void)
{
	SceUID module_ids[PSPWAVE_MAX_MODULES];
	int module_count = 0;
	int patched = 0;

	if (sceKernelGetModuleIdList(module_ids, sizeof(module_ids), &module_count) < 0) return 0;
	if (module_count > PSPWAVE_MAX_MODULES) module_count = PSPWAVE_MAX_MODULES;

	for (int i = 0; i < module_count; ++i)
	{
		SceKernelModuleInfo info;
		char *cursor;
		char *end;
		memset(&info, 0, sizeof(info));
		info.size = sizeof(info);
		if (sceKernelQueryModuleInfo(module_ids[i], &info) < 0) continue;
		if (!strstr(info.name, "system_plugin_bg") && !strstr(info.name, "sysconf_plugin") && !strstr(info.name, "vsh")) continue;
		cursor = (char *)info.text_addr;
		end = cursor + info.text_size + info.data_size;
		while (cursor < end - 34)
		{
			if (cursor[0] == 'f')
			{
				patched += replace_path(cursor, "flash0:/vsh/resource/01-12_03g.bmp", PSPWAVE_RESOURCE_1, 34);
				patched += replace_path(cursor, "flash0:/vsh/resource/01-12.bmp", PSPWAVE_RESOURCE_1, 30);
				patched += replace_path(cursor, "flash0:/vsh/resource/13-27.bmp", PSPWAVE_RESOURCE_2, 30);
			}
			++cursor;
		}
	}
	return patched;
}

static int redirect_thread(SceSize args, void *argp)
{
	(void)args;
	(void)argp;

	while (!patch_menu_palette()) sceKernelDelayThread(PSPWAVE_SCAN_DELAY_US);

	/* fail closed - a clean install intentionally keeps Sony's paths until the
	 * EBOOT has created and validated both byte-for-byte working copies */
	if (!resources_ready()) return sceKernelExitDeleteThread(0);
	while (!patch_background_paths()) sceKernelDelayThread(PSPWAVE_SCAN_DELAY_US);
	return sceKernelExitDeleteThread(0);
}

int module_start(SceSize args, void *argp)
{
	SceUID thread;
	if (sceKernelDevkitVersion() != PSPWAVE_FW_661) return 1;
	thread = sceKernelCreateThread("PSPWaveRedirect", redirect_thread, 0x18, 0x10000, 0, NULL);
	if (thread >= 0) sceKernelStartThread(thread, args, argp);
	return 0;
}

int module_stop(void)
{
	return 0;
}

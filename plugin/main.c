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

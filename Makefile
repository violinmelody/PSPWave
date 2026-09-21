TARGET = PSPWave
OBJS = src/main.o src/config.o src/themes.o src/wavegen.o src/render.o src/image.o
CFLAGS = -O2 -G0 -Wall -Wextra -Wshadow -Wstrict-prototypes
CXXFLAGS = $(CFLAGS) -fno-exceptions -fno-rtti
ASFLAGS = $(CFLAGS)
LIBS = -lpspgu -lpspdisplay -lm
EXTRA_TARGETS = EBOOT.PBP
PSP_EBOOT_TITLE = PSPWave
SFOFLAGS += -s APP_VER=01.01
PSP_EBOOT_SFO = PARAM.SFO
PSP_EBOOT_ICON = assets/ICON0.PNG
PSP_EBOOT_PIC1 = NULL
BUILD_PRX = 0
PSP_FW_VERSION = 661
INCDIR = include
include $(PSPSDK)/lib/build.mak

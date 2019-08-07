#
# Battery Logger for the RetroFW
#
# by pingflood; 2019
#

CHAINPREFIX=/opt/mipsel-linux-uclibc
CROSS_COMPILE=$(CHAINPREFIX)/usr/bin/mipsel-linux-

BUILDTIME=$(shell date +'\"%Y-%m-%d %H:%M\"')

CC = $(CROSS_COMPILE)gcc
CXX = $(CROSS_COMPILE)g++
STRIP = $(CROSS_COMPILE)strip

SYSROOT     := $(CHAINPREFIX)/usr/mipsel-buildroot-linux-uclibc/sysroot
SDL_CFLAGS  := $(shell $(SYSROOT)/usr/bin/sdl-config --cflags)
SDL_LIBS    := $(shell $(SYSROOT)/usr/bin/sdl-config --libs)

CFLAGS = -DTARGET_RETROFW -D__BUILDTIME__="$(BUILDTIME)" -DLOG_LEVEL=0 -g0 -Os $(SDL_CFLAGS) -I$(CHAINPREFIX)/usr/include/ -I$(SYSROOT)/usr/include/  -I$(SYSROOT)/usr/include/SDL/ -mhard-float -mips32 -mno-mips16
CFLAGS += -std=c++11 -fdata-sections -ffunction-sections -fno-exceptions -fno-math-errno -fno-threadsafe-statics -Isrc/

LDFLAGS = $(SDL_LIBS) -lfreetype -lSDL_image -lSDL_ttf -lSDL -lpthread
LDFLAGS +=-Wl,--as-needed -Wl,--gc-sections -s

pc:
	gcc src/battlog.c -g -o battlog/battlog.dge -lm -ggdb -O0 -DDEBUG -lSDL_image -lSDL -lSDL_ttf -I/usr/include/SDL

retrogame:
	$(CXX) $(CFLAGS) $(LDFLAGS) src/battlog.c -o battlog/battlog.dge

ipk: retrogame
	@rm -rf /tmp/.battlog-ipk/ && mkdir -p /tmp/.battlog-ipk/root/home/retrofw/apps/battlog /tmp/.battlog-ipk/root/home/retrofw/apps/gmenu2x/sections/applications
	@cp -r \
	battlog/battlog.dge \
	battlog/battlog.png \
	/tmp/.battlog-ipk/root/home/retrofw/apps/battlog

	@echo "title=Battlog" > /tmp/.battlog-ipk/root/home/retrofw/apps/gmenu2x/sections/applications/battlog.lnk
	@echo "description=Log battery discharge profile" >> /tmp/.battlog-ipk/root/home/retrofw/apps/gmenu2x/sections/applications/battlog.lnk
	@echo "exec=/home/retrofw/apps/battlog/battlog.dge" >> /tmp/.battlog-ipk/root/home/retrofw/apps/gmenu2x/sections/applications/battlog.lnk
	@echo "clock=600" >> /tmp/.battlog-ipk/root/home/retrofw/apps/gmenu2x/sections/applications/battlog.lnk

	@echo "/home/retrofw/apps/gmenu2x/sections/applications/battlog.lnk" > /tmp/.battlog-ipk/conffiles

	@echo "Package: battlog" > /tmp/.battlog-ipk/control
	@echo "Version: $$(date +%Y%m%d)" >> /tmp/.battlog-ipk/control
	@echo "Description: Battlog for the RetroFW" >> /tmp/.battlog-ipk/control
	@echo "Section: applications" >> /tmp/.battlog-ipk/control
	@echo "Priority: optional" >> /tmp/.battlog-ipk/control
	@echo "Maintainer: pingflood" >> /tmp/.battlog-ipk/control
	@echo "Architecture: mipsel" >> /tmp/.battlog-ipk/control
	@echo "Homepage: https://github.com/pingflood/battlog" >> /tmp/.battlog-ipk/control
	@echo "Depends:" >> /tmp/.battlog-ipk/control
	@echo "Source: https://github.com/pingflood/battlog" >> /tmp/.battlog-ipk/control

	@tar --owner=0 --group=0 -czvf /tmp/.battlog-ipk/control.tar.gz -C /tmp/.battlog-ipk/ control conffiles
	@tar --owner=0 --group=0 -czvf /tmp/.battlog-ipk/data.tar.gz -C /tmp/.battlog-ipk/root/ .
	@echo 2.0 > /tmp/.battlog-ipk/debian-binary
	@ar r battlog/battlog.ipk /tmp/.battlog-ipk/control.tar.gz /tmp/.battlog-ipk/data.tar.gz /tmp/.battlog-ipk/debian-binary

opk: retrogame
	@mksquashfs \
	battlog/default.retrofw.desktop \
	battlog/battlog.dge \
	battlog/battlog.png \
	battlog/battlog.opk \
	-all-root -noappend -no-exports -no-xattrs

clean:
	rm -rf battlog/battlog.dge battlog/battlog.ipk

# make target=x86_64-w64-mingw32 zip

target := $(shell gcc -dumpmachine)
out := _out/$(target)
libcache := $(out)/.cache
releases := _out/$(target).releases
ver := $(shell cat VERSION)

ld.extra := -Wl,-export-dynamic
pkg-config := pkg-config
progname := dummy-root-ca
rel := $(progname)-$(ver)
ifeq ($(target),x86_64-w64-mingw32)
pkg-config := x86_64-w64-mingw32-pkg-config
CC := x86_64-w64-mingw32-gcc
ld.extra := -Wl,-export-all-symbols #-mwindows
progname := dummy-root-ca.exe
endif

CFLAGS := $(shell $(pkg-config) --cflags gtk+-3.0) -g -Wall
LDFLAGS := $(shell $(pkg-config) --libs gtk+-3.0) $(ld.extra)

obj := $(patsubst %.c, $(libcache)/%.o, $(wildcard *.c))
static.dest := $(patsubst %, $(out)/%, gui.xml style.css dummy-root-ca.mk)

all := $(out)/$(progname) $(static.dest)
all: $(all)

$(out)/$(progname): $(obj)
	$(CC) $^ -o $@ $(LDFLAGS)

$(libcache)/%.o: %.c
	$(mkdir)
	$(CC) $< -o $@ -c $(CFLAGS)

$(static.dest): $(out)/%: %
	cp $< $@

export G_MESSAGES_DEBUG := dummy-root-ca

%.valgrind: %
	valgrind --log-file=$(libcache)/vgdump --track-origins=yes --leak-check=full --show-leak-kinds=definite --suppressions=/usr/share/gtk-3.0/valgrind/gtk.supp ./$*
	$(EDITOR) $(libcache)/vgdump &

mkdir = @mkdir -p $(dir $@)



zip := $(releases)/$(ver)/$(rel).zip
zip: $(zip)

ifeq ($(target),x86_64-w64-mingw32)
$(zip): $(all)
	rm -rf $(dir $@) $(releases)/$(rel).zip
	$(mkdir)/bin
	$(mkdir)/lib
	$(mkdir)/share/icons
	$(mkdir)/share/glib-2.0/schemas
	peldd $(out)/$(progname) --ignore-errors -t | xargs cp -t $(dir $@)/bin
	cp /usr/x86_64-w64-mingw32/sys-root/mingw/bin/gspawn-win64-helper* $(dir $@)/bin
	cp -t $(dir $@)/bin \
	 /usr/x86_64-w64-mingw32/sys-root/mingw/bin/libxml2-2.dll \
	 /usr/x86_64-w64-mingw32/sys-root/mingw/bin/librsvg-2-2.dll \
	 /usr/x86_64-w64-mingw32/sys-root/mingw/bin/liblzma-5.dll
	cp -t $(dir $@)/bin vendor/*
	cp -t $(dir $@)/bin $^
	cp -r /usr/x86_64-w64-mingw32/sys-root/mingw/lib/gdk-pixbuf-2.0 $(dir $@)/lib
	cp -r /usr/x86_64-w64-mingw32/sys-root/mingw/share/icons/Adwaita $(dir $@)/share/icons
	cp -r /usr/x86_64-w64-mingw32/sys-root/mingw/share/icons/hicolor $(dir $@)/share/icons
	cp -r /usr/x86_64-w64-mingw32/sys-root/mingw/share/glib-2.0/schemas $(dir $@)/share/glib-2.0
	cd $(dir $@) && zip -qr ../$(notdir $@) .
endif

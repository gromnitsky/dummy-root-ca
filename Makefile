# make target=x86_64-w64-mingw32 zip

target := $(shell gcc -dumpmachine)
out := _out/$(target)
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

static.dest := $(patsubst %, $(out)/%, gui.xml style.css dummy-root-ca.mk)

all := $(out)/$(progname) $(static.dest)
all: $(all)

define exe
$(mkdir)
$(CC) $< -o $@ $(CFLAGS) $(LDFLAGS)
endef

$(out)/%: %.c lib.c; $(exe)
$(out)/%.exe: %.c lib.c; $(exe)

$(static.dest): $(out)/%: %
	$(mkdir)
	cp $< $@

export G_MESSAGES_DEBUG := dummy-root-ca

%.valgrind: %
	valgrind --log-file=$(out)/.vgdump --track-origins=yes --leak-check=full --show-leak-kinds=definite --suppressions=/usr/share/gtk-3.0/valgrind/gtk.supp ./$*
	$(EDITOR) $(out)/.vgdump

mkdir = @mkdir -p $(dir $@)



zip := $(releases)/$(ver)/$(rel).zip
zip: $(zip)

# dnf install mingw64-gtk3 mingw64-hicolor-icon-theme mingw64-librsvg2 mingw64-xz
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
	mv $(dir $@)/bin/loaders.cache $(dir $@)/lib/gdk-pixbuf-2.0/2.10.0
	cp -r /usr/x86_64-w64-mingw32/sys-root/mingw/share/icons/Adwaita $(dir $@)/share/icons
	cp -r /usr/x86_64-w64-mingw32/sys-root/mingw/share/icons/hicolor $(dir $@)/share/icons
	cp -r /usr/x86_64-w64-mingw32/sys-root/mingw/share/glib-2.0/schemas $(dir $@)/share/glib-2.0
	cd $(dir $@) && zip -qr ../$(notdir $@) .
endif

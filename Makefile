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
ld.extra := -Wl,-export-all-symbols -mwindows
progname := dummy-root-ca.exe
endif

libs := gtk+-3.0 openssl
CFLAGS := $(shell $(pkg-config) --cflags $(libs)) -g -Wall -Werror
LDFLAGS := $(shell $(pkg-config) --libs $(libs)) $(ld.extra)

static.dest := $(patsubst %, $(out)/%, gui.xml style.css dummy-root-ca.mk)
obj :=

all := $(out)/$(progname) $(static.dest)
all: $(all)

define exe
$(mkdir)
$(CC) $< -o $@ $(obj) $(CFLAGS) $(LDFLAGS)
endef

$(out)/%: %.c lib.c ca.c; $(exe)

$(out)/%.exe: obj += $(out)/.cache/meta.res
$(out)/%.exe: %.c lib.c ca.c $(out)/.cache/meta.res
	$(exe)

$(static.dest): $(out)/%: %
	$(mkdir)
	cp $< $@

$(out)/.cache/meta.res: meta.rc app.ico
	$(mkdir)
	x86_64-w64-mingw32-windres $< -O coff -o $@

export G_MESSAGES_DEBUG := dummy-root-ca

%.valgrind: %
	valgrind --log-file=$(out)/.vgdump --track-origins=yes --leak-check=full --show-leak-kinds=definite --suppressions=/usr/share/gtk-3.0/valgrind/gtk.supp ./$*
	$(EDITOR) $(out)/.vgdump

mkdir = @mkdir -p $(dir $@)
SHELL := bash



zip := $(releases)/$(ver)/$(rel).zip
zip: $(zip)

# dnf install mingw64-gtk3 mingw64-hicolor-icon-theme mingw64-librsvg2
# https://github.com/gsauthof/pe-util
ifeq ($(target),x86_64-w64-mingw32)
mingw := /usr/x86_64-w64-mingw32/sys-root/mingw
$(zip): $(all)
	rm -rf $(dir $@) $(releases)/$(rel).zip
	$(mkdir)/{bin,lib,share/icons,share/glib-2.0/schemas}
	peldd $(out)/$(progname) --ignore-errors -t | xargs cp -t $(dir $@)/bin
	cp $(mingw)/bin/{libxml2-2.dll,librsvg-2-2.dll} $^ $(dir $@)/bin
	cp -r $(mingw)/lib/gdk-pixbuf-2.0 $(dir $@)/lib
	cp vendor/loaders.cache $(dir $@)/lib/gdk-pixbuf-2.0/2.10.0
	cp -r $(mingw)/share/icons/{Adwaita,hicolor} $(dir $@)/share/icons
	cp -r $(mingw)/share/glib-2.0/schemas $(dir $@)/share/glib-2.0
	cd $(dir $@) && zip -qr ../$(notdir $@) .
endif

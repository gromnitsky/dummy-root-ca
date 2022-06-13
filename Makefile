target := x86_64-redhat-linux
out := _out/$(target)
libcache := $(out)/.cache
ld.extra := -Wl,-export-dynamic
pkg-config := pkg-config

ifeq ($(target),x86_64-w64-mingw32)
pkg-config := x86_64-w64-mingw32-pkg-config
CC := x86_64-w64-mingw32-gcc
ld.extra := -Wl,--export-all-symbols
endif

CFLAGS := $(shell $(pkg-config) --cflags gtk+-3.0) -g -Wall
LDFLAGS := $(shell $(pkg-config) --libs gtk+-3.0) $(ld.extra)
obj := $(patsubst %.c, $(libcache)/%.o, $(wildcard *.c))

static.src := gui.xml style.css
static.dest := $(patsubst %, $(out)/%, $(static.src))

all: $(out)/dummy-root-ca $(static.dest)

$(out)/dummy-root-ca: $(obj)
	$(CC) $^ -o $@ $(LDFLAGS)

$(libcache)/%.o: %.c
	$(mkdir)
	$(CC) $< -o $@ -c $(CFLAGS)

$(static.dest): $(out)/%: %
	cp $< $@

export G_MESSAGES_DEBUG := dummy-root-ca

%.valgrind: %
	valgrind --log-file=$(out)/vgdump --track-origins=yes --leak-check=full --show-leak-kinds=definite --suppressions=/usr/share/gtk-3.0/valgrind/gtk.supp ./$*

mkdir = @mkdir -p $(dir $@)

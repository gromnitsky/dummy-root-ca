CFLAGS := -g -Wall -rdynamic $(shell pkg-config --cflags gtk+-3.0)
LDFLAGS := $(shell pkg-config --libs gtk+-3.0)

%.valgrind: %
	valgrind --track-origins=yes --leak-check=full --show-leak-kinds=definite --suppressions=/usr/share/gtk-3.0/valgrind/gtk.supp ./$*

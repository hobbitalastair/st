# st - simple terminal
# See LICENSE file for copyright and license details.
.POSIX:

include config.mk

WAYLAND_SCANNER = `$(PKG_CONFIG) --variable=wayland_scanner wayland-scanner`
WAYLAND_PROTOCOLS = `$(PKG_CONFIG) --variable=pkgdatadir wayland-protocols`
XDG_SHELL_XML = $(WAYLAND_PROTOCOLS)/stable/xdg-shell/xdg-shell.xml

PROTO = xdg-shell-protocol.h
SRC = st.c wayland.c $(PROTO:.h=.c)
OBJ = $(SRC:.c=.o)

all: st

config.h:
	cp config.def.h config.h

.c.o:
	$(CC) $(STCFLAGS) -c $<

st.o: config.h st.h win.h
wayland.o: arg.h config.h st.h win.h $(PROTO) drwl.h bufpool.h

$(OBJ): config.h config.mk

st: $(OBJ)
	$(CC) -o $@ $(OBJ) $(STLDFLAGS)

xdg-shell-protocol.h:
	$(WAYLAND_SCANNER) client-header $(XDG_SHELL_XML) $@
xdg-shell-protocol.c:
	$(WAYLAND_SCANNER) private-code $(XDG_SHELL_XML) $@

test:
	pytest tests/ -v

update-screenshots:
	rm -rf tests/screenshots/reference/*.png
	-pytest tests/ -v
	cp -r tests/screenshots/failed/*.png tests/screenshots/reference/
	rm -f tests/screenshots/reference/*.diff.png

clean:
	rm -f st $(OBJ) $(PROTO:.h=.c) $(PROTO) st-$(VERSION).tar.gz

dist: clean
	mkdir -p st-$(VERSION)
	cp -R LICENSE Makefile README config.mk \
		config.def.h st.info st.1 arg.h st.h win.h $(SRC) \
		st-$(VERSION)
	tar -cf - st-$(VERSION) | gzip > st-$(VERSION).tar.gz
	rm -rf st-$(VERSION)

install: st
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	cp -f st $(DESTDIR)$(PREFIX)/bin
	chmod 755 $(DESTDIR)$(PREFIX)/bin/st
	mkdir -p $(DESTDIR)$(MANPREFIX)/man1
	sed "s/VERSION/$(VERSION)/g" < st.1 > $(DESTDIR)$(MANPREFIX)/man1/st.1
	chmod 644 $(DESTDIR)$(MANPREFIX)/man1/st.1
	tic -sx st.info
	@echo Please see the README file regarding the terminfo entry of st.

uninstall:
	rm -f $(DESTDIR)$(PREFIX)/bin/st
	rm -f $(DESTDIR)$(MANPREFIX)/man1/st.1

.PHONY: all clean dist install uninstall

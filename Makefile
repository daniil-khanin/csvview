# Makefile for csvview

CC       = clang
CFLAGS   = -Wall -Wextra -g -O2
LDFLAGS  = -lncurses -lm -lpthread

TARGET   = csvview
MAN      = csvview.1

PREFIX   ?= /usr/local
BINDIR   = $(PREFIX)/bin
MANDIR   = $(PREFIX)/share/man/man1

OBJDIR   = obj
SOURCES  = $(wildcard src/*.c)
HEADERS  = $(wildcard src/*.h)
OBJECTS  = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SOURCES))

# ── build ──────────────────────────────────────────────
all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

$(OBJDIR)/%.o: src/%.c $(HEADERS) | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJDIR):
	mkdir -p $(OBJDIR)

# ── install / uninstall ────────────────────────────────
install: all
	install -d $(BINDIR)
	install -m 755 $(TARGET) $(BINDIR)/$(TARGET)
	install -d $(MANDIR)
	install -m 644 $(MAN) $(MANDIR)/$(MAN)

uninstall:
	rm -f $(BINDIR)/$(TARGET)
	rm -f $(MANDIR)/$(MAN)

# ── clean ──────────────────────────────────────────────
clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all install uninstall clean

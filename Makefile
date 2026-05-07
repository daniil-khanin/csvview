# Makefile for csvview

CC       = clang
CFLAGS   = -Wall -Wextra -g -O2

# Use ncursesw for proper wide-character (UTF-8) support
UNAME_S := $(shell uname -s)
UNAME_M := $(shell uname -m)
ifeq ($(UNAME_S),Darwin)
    # Pick the brew prefix that matches the build arch — on systems with
    # both Apple Silicon and Rosetta brew installs (`/opt/homebrew` vs
    # `/usr/local`), `brew` from PATH may point at the wrong arch and link
    # arm64 .o against x86_64 ncursesw. Prefer the arch-native brew.
    ifeq ($(UNAME_M),arm64)
        BREW ?= $(firstword $(wildcard /opt/homebrew/bin/brew) $(shell command -v brew 2>/dev/null))
        NCURSES_PREFIX ?= $(shell $(BREW) --prefix ncurses 2>/dev/null || echo /opt/homebrew/opt/ncurses)
    else
        BREW ?= $(firstword $(wildcard /usr/local/bin/brew) $(shell command -v brew 2>/dev/null))
        NCURSES_PREFIX ?= $(shell $(BREW) --prefix ncurses 2>/dev/null || echo /usr/local/opt/ncurses)
    endif
    CFLAGS  += -I$(NCURSES_PREFIX)/include -D_XOPEN_SOURCE_EXTENDED
    LDFLAGS  = -L$(NCURSES_PREFIX)/lib -lncursesw -lm -lpthread
else
    LDFLAGS  = -lncursesw -lm -lpthread
endif

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

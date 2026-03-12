# Makefile для csvview — все .c файлы в папке src/

CC       = clang
CFLAGS   = -Wall -Wextra -g -O2
LDFLAGS  = -lncurses

TARGET   = csvview.v11

# Все исходники теперь в src/
SOURCES  = $(wildcard src/*.c)
OBJECTS  = $(SOURCES:.c=.o)

# Папка для объектных файлов (опционально, можно и в src/)
OBJDIR   = obj
OBJECTS  = $(patsubst src/%.c,$(OBJDIR)/%.o,$(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

# Компиляция каждого .c → .o в папке obj/
$(OBJDIR)/%.o: src/%.c | $(OBJDIR)
	$(CC) $(CFLAGS) -c $< -o $@

# Создаём папку obj/, если её нет
$(OBJDIR):
	mkdir -p $(OBJDIR)

clean:
	rm -rf $(OBJDIR) $(TARGET)

.PHONY: all clean
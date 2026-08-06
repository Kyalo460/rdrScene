CC = x86_64-w64-mingw32-gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I./src -Isrc -I./raylib/raylib-5.5_win64_mingw-w64/include
LDFLAGS = -L./raylib/raylib-5.5_win64_mingw-w64/lib -lraylib -lopengl32 -lgdi32 -lwinmm
TARGET = nairobi_streets.exe

SRC_DIR = src
SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/world.c \
          $(SRC_DIR)/player.c \
          $(SRC_DIR)/enemy.c \
          $(SRC_DIR)/mission.c \
          $(SRC_DIR)/ui.c \
          $(SRC_DIR)/audio.c \
          $(SRC_DIR)/dialogue.c

OBJECTS = $(SOURCES:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	del /Q $(subst /,\,$(OBJECTS)) $(TARGET) 2>nul || true

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

.PHONY: all clean run debug
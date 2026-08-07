# Nairobi Life - realistic Nairobi street-life survival sim
# Build: mingw32-make
# Run:   mingw32-make run

CC      = gcc
RAYLIB  = ../raylib/raylib-5.5_win64_mingw-w64

CFLAGS  = -std=c99 -Wall -Wextra -O2 -ffast-math -Isrc -I$(RAYLIB)/include
LDFLAGS = -L$(RAYLIB)/lib -lraylib -lopengl32 -lgdi32 -lwinmm -lm

TARGET  = nairobi_life.exe
SRC_DIR = src
BUILD   = build

SOURCES = $(SRC_DIR)/main.c \
          $(SRC_DIR)/nl_util.c \
          $(SRC_DIR)/nl_time.c \
          $(SRC_DIR)/nl_weather.c \
          $(SRC_DIR)/nl_world.c \
          $(SRC_DIR)/nl_npc.c \
          $(SRC_DIR)/nl_econ.c \
          $(SRC_DIR)/nl_render.c \
          $(SRC_DIR)/nl_game.c

OBJECTS = $(patsubst $(SRC_DIR)/%.c,$(BUILD)/%.o,$(SOURCES))

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

$(BUILD)/%.o: $(SRC_DIR)/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD):
	-mkdir "$(BUILD)"

clean:
	-rm -rf "$(BUILD)" "$(TARGET)"

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -g -O0 -DDEBUG
debug: clean $(TARGET)

web:
	chmod +x build_web.sh
	./build_web.sh

.PHONY: all clean run debug web

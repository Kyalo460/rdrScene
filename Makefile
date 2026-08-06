CC = gcc
CFLAGS = -std=c99 -Wall -Wextra -O2 -I./src -Isrc
LDFLAGS = -lraylib -lopengl32 -lgdi32 -lwinmm
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

RAYLIB_SRC = /tmp/raylib-src/src
RAYLIB_SOURCES = $(RAYLIB_SRC)/rcore.c \
                 $(RAYLIB_SRC)/rshapes.c \
                 $(RAYLIB_SRC)/rtextures.c \
                 $(RAYLIB_SRC)/rtext.c \
                 $(RAYLIB_SRC)/rmodels.c \
                 $(RAYLIB_SRC)/raudio.c \
                 $(RAYLIB_SRC)/utils.c \
                 $(RAYLIB_SRC)/rglfw.c

EMCC = emcc
EMFLAGS = -std=c99 -Wall -Wextra -O2 -I./src -Isrc -I$(RAYLIB_SRC) -s USE_GLFW=3 -s ASYNCIFY -s TOTAL_MEMORY=67108864 -s FORCE_FILESYSTEM=1 -s EXPORTED_RUNTIME_METHODS=['ccall','cwrap'] -s EXPORTED_FUNCTIONS=['_main','_malloc','_free'] --shell-file shell.html
WEB_TARGET = nairobi_streets.html

all: $(TARGET)

$(TARGET): $(OBJECTS)
	$(CC) $(OBJECTS) -o $(TARGET) $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	del /Q $(subst /,\,$(OBJECTS)) $(TARGET) 2>nul || true
	del /Q $(WEB_TARGET) nairobi_streets.js nairobi_streets.wasm 2>nul || true

run: $(TARGET)
	./$(TARGET)

debug: CFLAGS += -g -DDEBUG
debug: $(TARGET)

web: $(SOURCES)
	git clone --depth 1 --branch 5.5 https://github.com/raysan5/raylib.git /tmp/raylib-src 2>/dev/null || true
	$(EMCC) $(SOURCES) $(RAYLIB_SOURCES) -o $(WEB_TARGET) $(EMFLAGS)

web-run: web
	python -m http.server 8080

.PHONY: all clean run debug web web-run
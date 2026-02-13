CC = $(CROSS_COMPILE)gcc

CFLAGS  = -Wall -Wno-unused -O2
LDLIBS  = -lSDL2 -lSDL2_image -lavformat -lavcodec -lavutil -lswresample

ifdef PREFIX
  # Cross-compile: headers and libraries live under PREFIX
  CFLAGS += -I$(PREFIX)/include -I$(PREFIX)/include/SDL2
  LDLIBS := -L$(PREFIX)/lib $(LDLIBS)
else
  # Native: use pkg-config
  PKG_CONFIG ?= pkg-config
  PKGS = sdl2 SDL2_image libavformat libavcodec libavutil libswresample
  CFLAGS += $(shell $(PKG_CONFIG) --cflags $(PKGS))
  LDLIBS  = $(shell $(PKG_CONFIG) --libs $(PKGS))
endif

BUILD_DIR = build
TARGET    = $(BUILD_DIR)/sdlamp2

.PHONY: all clean

all: $(TARGET)

$(TARGET): src/sdlamp2.c src/controls_png.h | $(BUILD_DIR)
	$(CC) $(CFLAGS) -o $@ $< $(LDLIBS)

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -f $(TARGET)

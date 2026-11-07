# Convenience wrapper around build.sh / idf.py.
#
#   make                 build (default target: esp32)
#   make flash           build + flash
#   make monitor         serial console
#   make run             build + flash + serial console
#   make ota             build + update over Wi-Fi (no USB needed)
#   make log             live log over Wi-Fi (from boot on); quit with Ctrl+C
#   make menuconfig      project options ("Nuki Matter Bridge")
#
# Override on the command line, e.g.:  make run PORT=/dev/cu.usbserial-0002
#                                      make ota HOST=192.168.178.135
#
# ┌──────────────────────────────────────────────────────────────────┐
# │  LEAVING THE SERIAL CONSOLE (monitor):                           │
# │    Ctrl + ]            (German Mac keyboard: Ctrl + Option + 9)  │
# │    or: Ctrl + T, then Ctrl + X                                   │
# │  Other keys: Ctrl+T Ctrl+R = reset board, Ctrl+T Ctrl+H = help   │
# └──────────────────────────────────────────────────────────────────┘

TARGET ?= esp32
PORT   ?= /dev/cu.usbserial-0001
BAUD   ?= 921600
# Matter's mDNS name of the board (its Wi-Fi MAC); the IP works as well.
HOST   ?= C8C9A3D0E0F0.local

ESPRESSIF_ROOT ?= /Volumes/mac-mini-external/espressif
BUILD_DIR := $(ESPRESSIF_ROOT)/builds/nuki_matter-$(TARGET)

IDF := ./build.sh $(TARGET)

define EXIT_HINT
	@echo ""
	@echo "  >>> Leave the monitor with: Ctrl + ]   (German keyboard: Ctrl + Option + 9)"
	@echo "  >>>                     or: Ctrl + T, then Ctrl + X"
	@echo ""
endef

.PHONY: all build flash monitor run ota log menuconfig size clean fullclean erase help

all: build

build:
	$(IDF) build

flash:
	$(IDF) -p $(PORT) -b $(BAUD) flash

monitor:
	$(EXIT_HINT)
	$(IDF) -p $(PORT) monitor

run:
	$(IDF) -p $(PORT) -b $(BAUD) flash
	$(EXIT_HINT)
	$(IDF) -p $(PORT) monitor

# Password comes from CONFIG_NUKI_OTA_PASSWORD in the build's sdkconfig.
ota: build
	@. ./env.sh >/dev/null && python tools/ota.py $(HOST) $(BUILD_DIR)/nuki_matter.bin $(BUILD_DIR)/sdkconfig

log:
	@nc $(HOST) 23

menuconfig:
	$(IDF) menuconfig

size:
	$(IDF) size

clean:
	$(IDF) clean

# Removes the whole build dir incl. its sdkconfig (simulate/unbolt settings!)
fullclean:
	$(IDF) fullclean

# Wipes the entire flash: Wi-Fi, Matter fabrics and Nuki pairing are lost
erase:
	$(IDF) -p $(PORT) erase-flash

help:
	@sed -n '2,19p' Makefile | sed 's/^# \{0,1\}//'

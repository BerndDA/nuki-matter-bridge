# Convenience wrapper around build.sh / idf.py.
#
#   make                 build (default target: esp32)
#   make flash           build + flash
#   make monitor         serial console
#   make run             build + flash + serial console
#   make menuconfig      project options ("Nuki Matter Bridge")
#
# Override on the command line, e.g.:  make run PORT=/dev/cu.usbserial-0002
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

IDF := ./build.sh $(TARGET)

define EXIT_HINT
	@echo ""
	@echo "  >>> Leave the monitor with: Ctrl + ]   (German keyboard: Ctrl + Option + 9)"
	@echo "  >>>                     or: Ctrl + T, then Ctrl + X"
	@echo ""
endef

.PHONY: all build flash monitor run menuconfig size clean fullclean erase help

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
	@sed -n '2,16p' Makefile | sed 's/^# \{0,1\}//'

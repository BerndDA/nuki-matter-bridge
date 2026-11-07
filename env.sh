# Usage: source ./env.sh   (from the repository root)
# Activates ESP-IDF v5.4.x. Paths come from config.local (see config.local.example),
# an existing IDF_PATH, or default to ~/esp/esp-idf-v5.4.4.
[ -f ./config.local ] && . ./config.local
ESPRESSIF_ROOT="${ESPRESSIF_ROOT:-$HOME/esp}"
IDF_DIR="${IDF_PATH:-$ESPRESSIF_ROOT/esp-idf-v5.4.4}"

if [ ! -f "$IDF_DIR/export.sh" ]; then
    echo "ESP-IDF not found at $IDF_DIR. Set ESPRESSIF_ROOT in config.local or IDF_PATH." >&2
    return 1 2>/dev/null || exit 1
fi

# Tools and component cache next to ESP-IDF when installed that way (keeps them off the system disk)
[ -d "$ESPRESSIF_ROOT/tools" ] && export IDF_TOOLS_PATH="$ESPRESSIF_ROOT/tools"
[ -d "$ESPRESSIF_ROOT/component-cache" ] && export IDF_COMPONENT_CACHE_PATH="$ESPRESSIF_ROOT/component-cache"
# macOS/Homebrew: IDF 5.4 needs Python <= 3.13; prefer 3.11 if it is installed
[ -d /opt/homebrew/opt/python@3.11/libexec/bin ] && export PATH="/opt/homebrew/opt/python@3.11/libexec/bin:$PATH"

. "$IDF_DIR/export.sh" > /dev/null
echo "ESP-IDF $(idf.py --version 2>/dev/null) ready"

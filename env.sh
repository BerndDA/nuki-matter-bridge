# Usage: source ./env.sh
# Activates ESP-IDF installed on the external drive (override with ESPRESSIF_ROOT).
ESPRESSIF_ROOT="${ESPRESSIF_ROOT:-/Volumes/mac-mini-external/espressif}"

if [ ! -d "$ESPRESSIF_ROOT/esp-idf-v5.4.4" ]; then
    echo "ESP-IDF not found under $ESPRESSIF_ROOT (is the external drive mounted?)" >&2
    return 1 2>/dev/null || exit 1
fi

export IDF_TOOLS_PATH="$ESPRESSIF_ROOT/tools"
# The IDF venv was created with Python 3.11 (IDF 5.4 doesn't support the default 3.15)
export PATH="/opt/homebrew/opt/python@3.11/libexec/bin:$PATH"
# Keep the component-manager cache off the (full) internal disk as well
export IDF_COMPONENT_CACHE_PATH="$ESPRESSIF_ROOT/component-cache"
. "$ESPRESSIF_ROOT/esp-idf-v5.4.4/export.sh" > /dev/null
echo "ESP-IDF $(idf.py --version 2>/dev/null) ready"

#!/bin/bash

# Путь к папке плагина
BUNDLE_PATH="$HOME/.lv2/disk_glitch.lv2"

# Проверка наличия папки и её создание при отсутствии
if [ ! -d "$BUNDLE_PATH" ]; then
	echo "Creating directory $BUNDLE_PATH..."
	mkdir -p "$BUNDLE_PATH"
fi

# 1. Компиляция (Senior-style: оптимизация под архитектуру Zen 2 / Ryzen 5700U)
gcc -O3 -march=znver2 -mtune=znver2 -ffast-math -shared -fPIC -DPIC $(pkg-config --cflags lv2) disk_glitch.c -o disk_glitch.so

# 2. Копирование бинарника и метаданных
cp disk_glitch.so manifest.ttl disk_glitch.ttl "$BUNDLE_PATH/"

echo "Done! Plugin updated in $BUNDLE_PATH"
echo "Restart Carla or click 'Reload' to see changes."

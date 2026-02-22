#!/bin/bash

# Путь к папке плагина
BUNDLE_PATH="$HOME/.lv2/peak_crusher.lv2"

# Проверка наличия папки и её создание при отсутствии
if [ ! -d "$BUNDLE_PATH" ]; then
	echo "Creating directory $BUNDLE_PATH..."
	mkdir -p "$BUNDLE_PATH"
fi

# Добавлена линковка -lm и -lmvec для исправления undefined symbol
# Добавлен флаг -s для тихой и компактной библиотеки
gcc -O3 -march=znver2 -mtune=znver2 -ffast-math -shared -fPIC -DPIC \
	$(pkg-config --cflags lv2) peak_crusher.c -o peak_crusher.so \
	-lm -lmvec -s

# 2. Копирование бинарника и метаданных
cp peak_crusher.so manifest.ttl peak_crusher.ttl "$BUNDLE_PATH/"

echo "Done! Plugin updated in $BUNDLE_PATH"
echo "Restart Carla or click 'Reload' to see changes."

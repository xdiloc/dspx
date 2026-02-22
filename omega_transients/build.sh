#!/bin/bash

# Путь к папке плагина
BUNDLE_PATH="$HOME/.lv2/omega_transients.lv2"

# Проверка наличия папки и её создание при отсутствии
if [ ! -d "$BUNDLE_PATH" ]; then
	echo "Creating directory $BUNDLE_PATH..."
	mkdir -p "$BUNDLE_PATH"
fi

# 1. Компиляция (Senior-style: оптимизация под архитектуру Zen 2 / Ryzen 5700U)
# -march=znver2: использование набора инструкций AVX2/FMA3
# -ffast-math: агрессивная оптимизация вещественных чисел для Hot Path
gcc -O3 -march=znver2 -mtune=znver2 -ffast-math -shared -fPIC -DPIC \
	$(pkg-config --cflags lv2) \
	omega_transients.c \
	../common/gain_normalizer.c \
	-I../common \
	-o omega_transients.so

# 2. Копирование бинарника и метаданных
cp omega_transients.so manifest.ttl omega_transients.ttl "$BUNDLE_PATH/"

echo "Done! Plugin updated in $BUNDLE_PATH"
echo "Restart Carla or click 'Reload' to see changes."

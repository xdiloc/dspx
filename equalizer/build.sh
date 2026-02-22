#!/bin/bash

# Путь к папке плагина
BUNDLE_PATH="$HOME/.lv2/dspx_equalizer.lv2"

# Проверка наличия папки и её создание при отсутствии
if [ ! -d "$BUNDLE_PATH" ]; then
	echo "Creating directory $BUNDLE_PATH..."
	mkdir -p "$BUNDLE_PATH"
fi

# 1. Компиляция (Senior-style)
# Добавлен флаг -lm для линковки математической библиотеки.
# Флаг -fno-math-errno помогает корректно обрабатывать векторизацию без побочных эффектов.
gcc -O3 -march=znver2 -mtune=znver2 -ffast-math -fno-math-errno -shared -fPIC -DPIC \
	$(pkg-config --cflags lv2) equalizer.c filter.c -o equalizer.so -lm

# 2. Копирование бинарника и метаданных
cp equalizer.so manifest.ttl equalizer.ttl "$BUNDLE_PATH/"

echo "Done! Plugin updated in $BUNDLE_PATH"
echo "Restart Carla or click 'Reload' to see changes."

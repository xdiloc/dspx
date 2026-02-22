#!/bin/bash

BUNDLE_PATH="$HOME/.lv2/stereo_expander.lv2"

# Проверка наличия папки и её создание при отсутствии
if [ ! -d "$BUNDLE_PATH" ]; then
	echo "Creating directory $BUNDLE_PATH..."
	mkdir -p "$BUNDLE_PATH"
fi

gcc -O3 -march=znver2 -mtune=znver2 -ffast-math -shared -fPIC -DPIC \
	$(pkg-config --cflags lv2) stereo_expander.c -o stereo_expander.so \
	-lm -lmvec -s

# Копирование бинарника и метаданных
cp stereo_expander.so manifest.ttl stereo_expander.ttl "$BUNDLE_PATH/"

echo "Done! Plugin updated in $BUNDLE_PATH"
echo "Restart Carla or click 'Reload' to see changes."

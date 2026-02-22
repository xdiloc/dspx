#!/bin/bash

# Путь к папке плагина
BUNDLE_PATH="$HOME/.lv2/audio_recorder.lv2"

# Проверка наличия папки и её создание при отсутствии
if [ ! -d "$BUNDLE_PATH" ]; then
	echo "Creating directory $BUNDLE_PATH..."
	mkdir -p "$BUNDLE_PATH"
fi

# 1. Компиляция (Senior-style: оптимизация под архитектуру Zen 2 / Ryzen 5700U)
# -march=znver2: включает AVX2, FMA3, BMI2, RDSEED и т.д.
# -mfma: ускоряет умножение-сложение в Hot Path
# -flto: Link Time Optimization для агрессивного инлайнинга
gcc -O3 -march=znver2 -mtune=znver2 -mavx2 -mfma -ffast-math -flto -shared -fPIC -DPIC \
	$(pkg-config --cflags lv2) audio_recorder.c -o audio_recorder.so -lsndfile

# 2. Копирование бинарника и метаданных
cp audio_recorder.so manifest.ttl audio_recorder.ttl "$BUNDLE_PATH/"

echo "Done! Plugin updated in $BUNDLE_PATH"
echo "Restart Carla or click 'Reload' to see changes."

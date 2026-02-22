#ifndef GAIN_NORMALIZER_H
#define GAIN_NORMALIZER_H

#include <stdint.h>

/**
 * @brief Константы нормализатора
 * LOOKBACK_SIZE - размер окна истории в блоках (~1-2 секунды при стандартных буферах)
 */
#define LOOKBACK_SIZE 200

/**
 * @brief Структура для хирургической нормализации громкости
 * current_gain - текущий примененный коэффициент (для плавности Lerp)
 * history_in   - массив максимальных пиков входа
 * history_out  - массив максимальных пиков выхода (до обработки нормализатором)
 * pos          - указатель текущей позиции в кольцевом буфере
 */
typedef struct {
	float current_gain;
	float history_in[LOOKBACK_SIZE];
	float history_out[LOOKBACK_SIZE];
	uint32_t pos;
	float cached_max_in;
	float cached_max_out;
} GainNormalizer;

/**
 * @brief Инициализация структуры
 * @param m Указатель на структуру GainNormalizer
 */
void gn_init(GainNormalizer* m);

/**
 * @brief Процесс нормализации сигнала Peak-to-Peak
 * * @param m              Указатель на нормализатор
 * @param outL           Левый канал (обрабатывается на месте)
 * @param outR           Правый канал (обрабатывается на месте)
 * @param sample_count   Количество сэмплов в блоке
 * @param block_peak_in  Максимальный абсолютный пик входного сигнала в этом блоке
 * @param block_peak_out Максимальный абсолютный пик выходного сигнала в этом блоке
 */
void gn_process(GainNormalizer* m, float* outL, float* outR, uint32_t sample_count, float block_peak_in, float block_peak_out);

#endif

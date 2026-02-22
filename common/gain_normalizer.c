#include "gain_normalizer.h"
#include <math.h>

/**
 * @brief Инициализация нормализатора
 * m - указатель на GainNormalizer
 */
void gn_init(GainNormalizer* m) {
	m->current_gain = 1.0f;
	m->pos = 0;
	m->cached_max_in = 1e-6f;
	m->cached_max_out = 1e-6f;
	for (int i = 0; i < LOOKBACK_SIZE; i++) {
		m->history_in[i] = 1e-6f; 
		m->history_out[i] = 1e-6f;
	}
}

/**
 * @brief Процесс нормализации Peak-to-Peak (Hot Path)
 * @param m              Указатель на структуру
 * @param outL           Левый канал
 * @param outR           Правый канал
 * @param sample_count   Размер блока
 * @param block_peak_in  Макс. абсолютный пик входа
 * @param block_peak_out Макс. абсолютный пик выхода до обработки
 */
void gn_process(GainNormalizer* m, float* outL, float* outR, uint32_t sample_count, float block_peak_in, float block_peak_out) {
	const float val_in  = (block_peak_in > 1e-6f) ? block_peak_in : 1e-6f;
	const float val_out = (block_peak_out > 1e-6f) ? block_peak_out : 1e-6f;

	// Запоминаем старые значения, которые сейчас затрем
	float old_in  = m->history_in[m->pos];
	float old_out = m->history_out[m->pos];

	m->history_in[m->pos]  = val_in;
	m->history_out[m->pos] = val_out;
	m->pos = (m->pos + 1) % LOOKBACK_SIZE;

	// Быстрое сравнение (Branchless style)
	// Если новый пик больше кэша - обновляем кэш
	if (val_in > m->cached_max_in) m->cached_max_in = val_in;
	if (val_out > m->cached_max_out) m->cached_max_out = val_out;

	// Если мы затерли старый максимум, нужно пересчитать кэш (Full Scan)
	if (old_in >= m->cached_max_in || old_out >= m->cached_max_out) {
		float new_max_in = 1e-6f;
		float new_max_out = 1e-6f;
		for (int k = 0; k < LOOKBACK_SIZE; ++k) {
			new_max_in  = (m->history_in[k]  > new_max_in)  ? m->history_in[k]  : new_max_in;
			new_max_out = (m->history_out[k] > new_max_out) ? m->history_out[k] : new_max_out;
		}
		m->cached_max_in = new_max_in;
		m->cached_max_out = new_max_out;
	}

	float target_gain = m->cached_max_in / m->cached_max_out;
	if (target_gain > 4.0f) target_gain = 4.0f;

	float active_g = m->current_gain;
	const float g_step = (target_gain - active_g) / (float)sample_count;

	for (uint32_t i = 0; i < sample_count; ++i) {
		active_g += g_step;
		outL[i] *= active_g;
		outR[i] *= active_g;
	}

	m->current_gain = active_g;
}

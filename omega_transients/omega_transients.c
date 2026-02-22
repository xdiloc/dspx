#include <lv2/core/lv2.h>
#include <stdlib.h>
#include <math.h>
#include "../common/gain_normalizer.h"

#define OMEGA_ATTACK_PORT  0
#define OMEGA_RELEASE_PORT 1
#define OMEGA_INPUT_L      2
#define OMEGA_INPUT_R      3
#define OMEGA_OUTPUT_L     4
#define OMEGA_OUTPUT_R     5

typedef struct {
	float lastL, lastR;
	GainNormalizer normalizer;
	const float* attack_gain;
	const float* release_time;
	const float* inputL;
	const float* inputR;
	float* outputL;
	float* outputR;
	float sample_rate;
} OmegaTransients;

/**
 * @brief Инициализация плагина
 * descriptor - дескриптор
 * rate - частота дискретизации
 * bundle_path - путь
 * features - фичи
 */
static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
	OmegaTransients* m = (OmegaTransients*)calloc(1, sizeof(OmegaTransients));
	if (m) {
		m->sample_rate = (float)rate;
		gn_init(&m->normalizer);
	}
	return (LV2_Handle)m;
}

/**
 * @brief Подключение портов
 * instance - экземпляр
 * port - номер порта
 * data - указатель
 */
static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
	OmegaTransients* m = (OmegaTransients*)instance;
	switch (port) {
		case OMEGA_ATTACK_PORT:  m->attack_gain = (const float*)data; break;
		case OMEGA_RELEASE_PORT: m->release_time = (const float*)data; break;
		case OMEGA_INPUT_L:      m->inputL = (const float*)data; break;
		case OMEGA_INPUT_R:      m->inputR = (const float*)data; break;
		case OMEGA_OUTPUT_L:     m->outputL = (float*)data; break;
		case OMEGA_OUTPUT_R:     m->outputR = (float*)data; break;
	}
}

/**
 * @brief Основной цикл обработки (Hot Path) с работающим Release
 * instance - экземпляр плагина
 * sample_count - размер блока
 */
static void run(LV2_Handle instance, uint32_t sample_count) {
	OmegaTransients* m = (OmegaTransients*)instance;
	const float attack = *m->attack_gain;
	const float rel_ms = *m->release_time; // Читаем значение из порта
	
	// Конвертируем ms в коэффициент затухания (Strength Reduction: вынесено из цикла)
	// Приближенная формула для 1-pole фильтра
	const float release_decay = expf(-1.0f / (m->sample_rate * (rel_ms * 0.001f)));

	const float* __restrict__ inL = m->inputL;
	const float* __restrict__ inR = m->inputR;
	float* __restrict__ outL = m->outputL;
	float* __restrict__ outR = m->outputR;

	float lastL = m->lastL;
	float lastR = m->lastR;
	float block_peak_in = 0.0f;
	float block_peak_out = 0.0f;

	for (uint32_t i = 0; i < sample_count; ++i) {
		const float curL = inL[i];
		const float curR = inR[i];

		// Пики для Gain Normalizer
		const float abs_inL = fabsf(curL);
		const float abs_inR = fabsf(curR);
		block_peak_in = (abs_inL > block_peak_in) ? abs_inL : block_peak_in;
		block_peak_in = (abs_inR > block_peak_in) ? abs_inR : block_peak_in;

		float dL = curL - lastL;
		float dR = curR - lastR;

		// Branchless Clamp
		dL = (dL > 0.5f) ? 0.5f : ((dL < -0.5f) ? -0.5f : dL);
		dR = (dR > 0.5f) ? 0.5f : ((dR < -0.5f) ? -0.5f : dR);

		const float procL = curL + (dL * attack);
		const float procR = curR + (dR * attack);
		
		outL[i] = procL;
		outR[i] = procR;

		const float abs_outL = fabsf(procL);
		const float abs_outR = fabsf(procR);
		block_peak_out = (abs_outL > block_peak_out) ? abs_outL : block_peak_out;
		block_peak_out = (abs_outR > block_peak_out) ? abs_outR : block_peak_out;

		// Теперь крутилка Release влияет на скорость обновления "памяти" плагина
		lastL = curL * release_decay;
		lastR = curR * release_decay;
	}

	m->lastL = lastL;
	m->lastR = lastR;

	gn_process(&m->normalizer, outL, outR, sample_count, block_peak_in, block_peak_out);
}

/**
 * @brief Очистка
 * instance - экземпляр
 */
static void cleanup(LV2_Handle instance) { free(instance); }

static const LV2_Descriptor descriptor = {
	"https://xdiloc.github.io/dspx/omega-transients", instantiate, connect_port, NULL, run, NULL, cleanup
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
	return (index == 0) ? &descriptor : NULL;
}

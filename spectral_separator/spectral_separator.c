#include <lv2/core/lv2.h>
#include <stdlib.h>
#include <math.h>
#include "../common/gain_normalizer.h"

#define SPECTRAL_CONTRAST_PORT 0
#define SPECTRAL_SHARP_PORT    1
#define SPECTRAL_MIX_PORT      2
#define SPECTRAL_TILT_PORT     3
#define SPECTRAL_INPUT_L       4
#define SPECTRAL_INPUT_R       5
#define SPECTRAL_OUTPUT_L      6
#define SPECTRAL_OUTPUT_R      7

/**
 * @brief Структура SpectralSeparator
 * lastL - предыдущий сэмпл левого канала
 * lastR - предыдущий сэмпл правого канала
 * gn - модуль нормализации гейна
 * contrast - указатель на порт контраста
 * sharp - указатель на порт резкости (presence)
 * mix - указатель на порт соотношения dry/wet
 * tilt - указатель на порт частотного наклона
 * inputL - входной буфер левого канала
 * inputR - входной буфер правого канала
 * outputL - выходной буфер левого канала
 * outputR - выходной буфер правого канала
 */
typedef struct {
	float lastL;
	float lastR;
	GainNormalizer gn;
	const float* contrast;
	const float* sharp;
	const float* mix;
	const float* tilt;
	const float* inputL;
	const float* inputR;
	float* outputL;
	float* outputR;
} SpectralSeparator;

/**
 * @brief Создание экземпляра
 * descriptor - дескриптор плагина
 * rate - частота дискретизации
 * bundle_path - путь к ресурсам
 * features - дополнительные возможности LV2
 */
static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
	SpectralSeparator* m = (SpectralSeparator*)calloc(1, sizeof(SpectralSeparator));
	if (m) {
		gn_init(&m->gn);
	}
	return (LV2_Handle)m;
}

/**
 * @brief Подключение портов
 * instance - экземпляр плагина
 * port - индекс порта
 * data - указатель на данные порта
 */
static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
	SpectralSeparator* m = (SpectralSeparator*)instance;
	switch (port) {
		case SPECTRAL_CONTRAST_PORT: m->contrast = (const float*)data; break;
		case SPECTRAL_SHARP_PORT:    m->sharp = (const float*)data; break;
		case SPECTRAL_MIX_PORT:      m->mix = (const float*)data; break;
		case SPECTRAL_TILT_PORT:     m->tilt = (const float*)data; break;
		case SPECTRAL_INPUT_L:       m->inputL = (const float*)data; break;
		case SPECTRAL_INPUT_R:       m->inputR = (const float*)data; break;
		case SPECTRAL_OUTPUT_L:      m->outputL = (float*)data; break;
		case SPECTRAL_OUTPUT_R:      m->outputR = (float*)data; break;
	}
}

/**
 * @brief Hot Path: Spectral Processing
 * instance - экземпляр плагина
 * sample_count - количество сэмплов для обработки
 */
static void run(LV2_Handle instance, uint32_t sample_count) {
	SpectralSeparator* m = (SpectralSeparator*)instance;

	const float p_contrast = *m->contrast;
	const float p_presence = *m->sharp;
	const float mix_val    = *m->mix; 
	const float p_tilt     = *m->tilt;
	const float inv_mix    = 1.0f - mix_val;

	const float* __restrict__ inL = m->inputL;
	const float* __restrict__ inR = m->inputR;
	float* __restrict__ outL = m->outputL;
	float* __restrict__ outR = m->outputR;

	float l_prev = m->lastL;
	float r_prev = m->lastR;

	float block_peak_in = 1e-9f;
	float block_peak_out = 1e-9f;

	const float sideSlope = 0.35f;
	uint32_t i;

	for (i = 0; i < sample_count; ++i) {
		const float vL = inL[i];
		const float vR = inR[i];

		const float absL = fabsf(vL);
		const float absR = fabsf(vR);

		block_peak_in = fmaxf(block_peak_in, fmaxf(absL, absR));

		/* Обработка Left */
		const float dL = vL - l_prev;
		l_prev = vL;
		float procL = dL * (p_contrast - (fabsf(dL) * sideSlope) + p_tilt);
		const float wetL = (dL * 0.88f + procL) * p_presence;
		outL[i] = (vL * mix_val) + (wetL * inv_mix);

		/* Обработка Right */
		const float dR = vR - r_prev;
		r_prev = vR;
		float procR = dR * (p_contrast - (fabsf(dR) * sideSlope) + p_tilt);
		const float wetR = (dR * 0.88f + procR) * p_presence;
		outR[i] = (vR * mix_val) + (wetR * inv_mix);

		block_peak_out = fmaxf(block_peak_out, fmaxf(fabsf(outL[i]), fabsf(outR[i])));
	}

	m->lastL = l_prev;
	m->lastR = r_prev;

	gn_process(&m->gn, outL, outR, sample_count, block_peak_in, block_peak_out);
}

/**
 * @brief Очистка ресурсов
 * instance - экземпляр плагина
 */
static void cleanup(LV2_Handle instance) { 
	free(instance); 
}

static const LV2_Descriptor descriptor = {
	"https://xdiloc.github.io/dspx/spectral-separator", instantiate, connect_port, NULL, run, NULL, cleanup
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
	return (index == 0) ? &descriptor : NULL;
}

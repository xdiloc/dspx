#include <lv2/core/lv2.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>
#include "filter.h"

#define BANDS_COUNT 10

typedef struct {
	const float* ports[BANDS_COUNT];
	const float* preamp_port;
	const float* mode_port;
	const float* inputL;
	const float* inputR;
	float* outputL;
	float* outputR;
	
	BandFilter filters[BANDS_COUNT];
	float last_preamp_db;
	float preamp_gain;
	double sample_rate;
	int last_mode;
} dspxEq;

/**
 * @brief Обновление коэффициентов усиления предусилителя
 * m - указатель на структуру плагина
 * p_db - текущее значение усиления в дБ
 */
static void update_preamp(dspxEq* m, float p_db) {
	if (m->last_preamp_db != p_db) {
		m->preamp_gain = powf(10.0f, p_db * 0.05f);
		m->last_preamp_db = p_db;
	}
}

/**
 * @brief Обновление параметров фильтров
 * m - указатель на структуру плагина
 * mode - текущий режим фильтрации
 */
static void update_filter_params(dspxEq* m, int mode) {
	int b;
	float fs = (float)m->sample_rate;

	for (b = 0; b < BANDS_COUNT; ++b) {
		if (mode == FORM_SVF) {
			compute_coeffs_svf(&m->filters[b], *m->ports[b], fs);
		} else {
			compute_coeffs_biquad(&m->filters[b], *m->ports[b], fs);
		}
	}
}

/**
 * @brief Создание экземпляра плагина
 * desc - дескриптор плагина
 * sr - частота дискретизации
 */
static LV2_Handle instantiate(const LV2_Descriptor* desc, double sr, const char* path, const LV2_Feature* const* features) {
	dspxEq* m = (dspxEq*)calloc(1, sizeof(dspxEq));
	int i;
	static const float freqs[10] = {60, 170, 310, 600, 1000, 3000, 6000, 12000, 14000, 16000};
	if (!m) return NULL;
	m->sample_rate = sr;
	m->last_preamp_db = -999.0f;
	m->last_mode = -1;
	for (i = 0; i < BANDS_COUNT; ++i) {
		m->filters[i].freq = freqs[i];
		m->filters[i].type = (i == 0) ? BT_LOWSHELF : (i == 9 ? BT_HIGHSHELF : BT_PEAKING);
		m->filters[i].last_gain = -999.0f;
		m->filters[i].v1L = m->filters[i].v2L = 0;
		m->filters[i].v1R = m->filters[i].v2R = 0;
	}
	return (LV2_Handle)m;
}

static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
	dspxEq* m = (dspxEq*)instance;
	if (port < 10) m->ports[port] = (const float*)data;
	else if (port == 10) m->preamp_port = (const float*)data;
	else if (port == 11) m->mode_port = (const float*)data;
	else if (port == 12) m->inputL = (const float*)data;
	else if (port == 13) m->inputR = (const float*)data;
	else if (port == 14) m->outputL = (float*)data;
	else if (port == 15) m->outputR = (float*)data;
}

/**
 * @brief Обработка аудио
 * instance - экземпляр плагина
 * n_samples - количество семплов в блоке
 */
static void run(LV2_Handle instance, uint32_t n_samples) {
	dspxEq* m = (dspxEq*)instance;
	float p_db = *m->preamp_port;
	int mode = (int)*m->mode_port;
	uint32_t s;
	int b;

	if (m->last_mode != mode) {
		for (b = 0; b < BANDS_COUNT; ++b) {
			m->filters[b].v1L = m->filters[b].v2L = 0;
			m->filters[b].v1R = m->filters[b].v2R = 0;
			m->filters[b].last_gain = -999.0f;
		}
		m->last_mode = mode;
	}

	update_preamp(m, p_db);
	update_filter_params(m, mode);

	const float pg = m->preamp_gain;
	if (mode == FORM_SVF) {
		for (s = 0; s < n_samples; ++s) {
			float l = m->inputL[s] * pg;
			float r = m->inputR[s] * pg;
			for (b = 0; b < BANDS_COUNT; ++b) {
				process_svf(&m->filters[b], &l, &r);
			}
			m->outputL[s] = l;
			m->outputR[s] = r;
		}
	} else if (mode == FORM_TDF2) {
		for (s = 0; s < n_samples; ++s) {
			float l = m->inputL[s] * pg;
			float r = m->inputR[s] * pg;
			for (b = 0; b < BANDS_COUNT; ++b) {
				process_tdf2(&m->filters[b], &l, &r);
			}
			m->outputL[s] = l;
			m->outputR[s] = r;
		}
	} else {
		for (s = 0; s < n_samples; ++s) {
			float l = m->inputL[s] * pg;
			float r = m->inputR[s] * pg;
			for (b = 0; b < BANDS_COUNT; ++b) {
				process_df2(&m->filters[b], &l, &r);
			}
			m->outputL[s] = l;
			m->outputR[s] = r;
		}
	}
}

static void cleanup(LV2_Handle instance) { free(instance); }

static const LV2_Descriptor descriptor = {
	"https://xdiloc.github.io/dspx/equalizer", instantiate, connect_port, NULL, run, NULL, cleanup, NULL
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) { return (index == 0) ? &descriptor : NULL; }

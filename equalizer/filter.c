#include "filter.h"

/**
 * @brief Расчет стандартных коэффициентов биквадрата
 * f - указатель на структуру фильтра
 * gain_db - усиление полосы в децибелах
 * sr - частота дискретизации
 */
void compute_coeffs_biquad(BandFilter* f, float gain_db, float sr) {
	float a, w0, cw0, sn0, alpha, sa;
	float a0, a1, a2, b0, b1, b2;
	float inv_a0, ap1, am1;
	/* Q для Peaking и S (slope) для Shelf */
	const float q_factor = 0.7071067811865476f; 
	const float shelf_slope = 1.0f;

	if (f->last_gain == gain_db) return;
	f->last_gain = gain_db;

	/* A = 10^(dBgain/40) */
	a = powf(10.0f, gain_db * 0.025f);
	w0 = 2.0f * PI * f->freq / sr;
	cw0 = cosf(w0);
	sn0 = sinf(w0);

	/* Strength Reduction для Shelf-фильтров: вычисляем alpha через Slope */
	if (f->type == BT_PEAKING) {
		alpha = sn0 / (2.0f * q_factor);
	} else {
		/* W3C Shelf alpha: sin(w0)/2 * sqrt( (A + 1/A)*(1/S - 1) + 2 ) */
		alpha = (sn0 / 2.0f) * sqrtf((a + 1.0f / a) * (1.0f / shelf_slope - 1.0f) + 2.0f);
	}
	
	sa = 2.0f * sqrtf(a) * alpha;
	ap1 = a + 1.0f;
	am1 = a - 1.0f;

	if (f->type == BT_LOWSHELF) {
		/* H(s) = A * (s^2 + (sqrt(A)/Q)*s + A) / (A*s^2 + (sqrt(A)/Q)*s + 1) */
		b0 = a * (ap1 - am1 * cw0 + sa);
		b1 = 2.0f * a * (am1 - ap1 * cw0);
		b2 = a * (ap1 - am1 * cw0 - sa);
		a0 = ap1 + am1 * cw0 + sa;
		a1 = -2.0f * (am1 + ap1 * cw0);
		a2 = ap1 + am1 * cw0 - sa;
	} else if (f->type == BT_HIGHSHELF) {
		/* H(s) = A * (A*s^2 + (sqrt(A)/Q)*s + 1) / (s^2 + (sqrt(A)/Q)*s + A) */
		b0 = a * (ap1 + am1 * cw0 + sa);
		b1 = -2.0f * a * (am1 + ap1 * cw0);
		b2 = a * (ap1 + am1 * cw0 - sa);
		a0 = ap1 - am1 * cw0 + sa;
		a1 = 2.0f * (am1 - ap1 * cw0);
		a2 = ap1 - am1 * cw0 - sa;
	} else {
		/* Peaking EQ */
		b0 = 1.0f + alpha * a;
		b1 = -2.0f * cw0;
		b2 = 1.0f - alpha * a;
		a0 = 1.0f + alpha / a;
		a1 = -2.0f * cw0;
		a2 = 1.0f - alpha / a;
	}

	inv_a0 = 1.0f / a0;
	f->b0 = b0 * inv_a0;
	f->b1 = b1 * inv_a0;
	f->b2 = b2 * inv_a0;
	f->a1 = a1 * inv_a0;
	f->a2 = a2 * inv_a0;
}

/**
 * @brief Расчет специфичных коэффициентов для SVF
 * f - указатель на структуру фильтра
 * gain_db - усиление полосы в децибелах
 * sr - частота дискретизации
 */
void compute_coeffs_svf(BandFilter* f, float gain_db, float sr) {
	if (f->last_gain == gain_db) return;
	f->last_gain = gain_db;

	float g = tanf(PI * f->freq / sr);
	float k = 1.41421356f; 
	float a1 = 1.0f / (1.0f + g * (g + k));

	f->svf_a1 = a1;
	f->svf_a2 = g * a1;
	f->svf_a3 = g * f->svf_a2;
	f->svf_m_gain = powf(10.0f, gain_db * 0.05f) - 1.0f;
}

/**
 * @brief Обработка одного канала фильтром Direct Form II
 * f - указатель на структуру фильтра
 * in - входной сигнал
 * v1 - указатель на состояние 1
 * v2 - указатель на состояние 2
 */
static float process_channel_df2(BandFilter* f, float in, float* v1, float* v2) {
	const float leakage = 0.99f;
	float w = in - f->a1 * (*v1) - f->a2 * (*v2);
	float out = f->b0 * w + f->b1 * (*v1) + f->b2 * (*v2);

	if (in == 0.0f) {
		/* Гасим энергию в узлах */
		*v2 = (*v1) * leakage;
		*v1 = w * leakage;
		/* Проверка на денормалы */
		if (fabsf(*v1) < 1e-12f) { 
			*v1 = 0.0f; *v2 = 0.0f; out = 0.0f; 
		}
	} else {
		*v2 = *v1;
		*v1 = w;
	}
	return out;
}

/**
 * @brief Обработка одной полосы методом Direct Form II
 * f - структура фильтра
 * l - указатель на левый канал
 * r - указатель на правый канал
 */
void process_df2(BandFilter* f, float* l, float* r) {
	*l = process_channel_df2(f, *l, &f->v1L, &f->v2L);
	*r = process_channel_df2(f, *r, &f->v1R, &f->v2R);
}

/**
 * @brief Обработка одного канала фильтром Transposed Direct Form II
 * f - указатель на структуру фильтра
 * in - входной сигнал
 * v1 - указатель на состояние 1
 * v2 - указатель на состояние 2
 */
static float process_channel_tdf2(BandFilter* f, float in, float* v1, float* v2) {
	const float leakage = 0.99f;
	float out = f->b0 * in + (*v1);
	float next_v1 = f->b1 * in - f->a1 * out + (*v2);
	float next_v2 = f->b2 * in - f->a2 * out;

	if (in == 0.0f) {
		/* Применяем гаситель к результату шага */
		*v1 = next_v1 * leakage;
		*v2 = next_v2 * leakage;

		/* Плавное обнуление при достижении порога */
		if (fabsf(*v1) < 1e-12f) {
			*v1 = 0.0f; *v2 = 0.0f; out = 0.0f;
		}
	} else {
		*v1 = next_v1;
		*v2 = next_v2;
	}
	return out;
}

/**
 * @brief Обработка одной полосы методом Transposed Direct Form II
 * f - структура фильтра
 * l - указатель на левый канал
 * r - указатель на правый канал
 */
void process_tdf2(BandFilter* f, float* l, float* r) {
	*l = process_channel_tdf2(f, *l, &f->v1L, &f->v2L);
	*r = process_channel_tdf2(f, *r, &f->v1R, &f->v2R);
}

/**
 * @brief Обработка одного канала фильтром State Variable Filter
 * f - указатель на структуру фильтра
 * in - входной сигнал
 * v1 - указатель на состояние 1
 * v2 - указатель на состояние 2
 */
static float process_channel_svf(BandFilter* f, float in, float* v1, float* v2) {
	const float a1 = f->svf_a1;
	const float a2 = f->svf_a2;
	const float a3 = f->svf_a3;
	const float mg = f->svf_m_gain;
	const float leakage = 0.99f;

	float v3 = in - (*v2);
	float v1_step = a1 * (*v1) + a2 * v3;
	float v2_step = (*v2) + a2 * (*v1) + a3 * v3;

	float out = in + mg * v1_step;

	/* Обновление состояний по методу трапеций */
	float next_v1 = 2.0f * v1_step - (*v1);
	float next_v2 = 2.0f * v2_step - (*v2);

	if (in == 0.0f) {
		*v1 = next_v1 * leakage;
		*v2 = next_v2 * leakage;
		if (fabsf(*v1) < 1e-12f) { *v1 = 0.0f; *v2 = 0.0f; out = 0.0f; }
	} else {
		*v1 = next_v1;
		*v2 = next_v2;
	}
	return out;
}

/**
 * @brief Обработка одной полосы методом State Variable Filter
 * f - указатель на структуру фильтра
 * l - левый канал
 * r - правый канал
 */
void process_svf(BandFilter* f, float* l, float* r) {
	*l = process_channel_svf(f, *l, &f->v1L, &f->v2L);
	*r = process_channel_svf(f, *r, &f->v1R, &f->v2R);
}


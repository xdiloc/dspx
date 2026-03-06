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
 * @brief Обработка одной полосы методом Direct Form II
 * f - структура фильтра
 * l - указатель на левый канал
 * r - указатель на правый канал
 */
void process_df2(BandFilter* f, float* l, float* r) {
	const float inL = *l;
	const float inR = *r;
	const float leakage = 0.99f;
	float outL, outR;

	/* --- Левый канал --- */
	/* Общая математика DF2 */
	float wL = inL - f->a1 * f->v1L - f->a2 * f->v2L;
	outL = f->b0 * wL + f->b1 * f->v1L + f->b2 * f->v2L;

	if (inL == 0.0f) {
		/* Гасим энергию в узлах */
		f->v2L = f->v1L * leakage;
		f->v1L = wL * leakage;
		/* Проверка на денормалы */
		if (fabsf(f->v1L) < 1e-12f) { 
			f->v1L = 0.0f; f->v2L = 0.0f; outL = 0.0f; 
		}
	} else {
		f->v2L = f->v1L;
		f->v1L = wL;
	}
	*l = outL;

	/* --- Правый канал --- */
	float wR = inR - f->a1 * f->v1R - f->a2 * f->v2R;
	outR = f->b0 * wR + f->b1 * f->v1R + f->b2 * f->v2R;

	if (inR == 0.0f) {
		f->v2R = f->v1R * leakage;
		f->v1R = wR * leakage;
		if (fabsf(f->v1R) < 1e-12f) { 
			f->v1R = 0.0f; f->v2R = 0.0f; outR = 0.0f; 
		}
	} else {
		f->v2R = f->v1R;
		f->v1R = wR;
	}
	*r = outR;
}

/**
 * @brief Обработка одной полосы методом Transposed Direct Form II
 * f - структура фильтра
 * l - указатель на левый канал
 * r - указатель на правый канал
 */
void process_tdf2(BandFilter* f, float* l, float* r) {
	const float inL = *l;
	const float inR = *r;
	const float leakage = 0.99f;
	float outL, outR;

	/* --- Левый канал --- */
	/* Вычисляем стандартный шаг TDF2 */
	outL = f->b0 * inL + f->v1L;
	float next_v1L = f->b1 * inL - f->a1 * outL + f->v2L;
	float next_v2L = f->b2 * inL - f->a2 * outL;

	if (inL == 0.0f) {
		/* Применяем гаситель к результату шага */
		f->v1L = next_v1L * leakage;
		f->v2L = next_v2L * leakage;

		/* Плавное обнуление при достижении порога */
		if (fabsf(f->v1L) < 1e-12f) {
			f->v1L = 0.0f; f->v2L = 0.0f; outL = 0.0f;
		}
	} else {
		f->v1L = next_v1L;
		f->v2L = next_v2L;
	}
	*l = outL;

	/* --- Правый канал --- */
	outR = f->b0 * inR + f->v1R;
	float next_v1R = f->b1 * inR - f->a1 * outR + f->v2R;
	float next_v2R = f->b2 * inR - f->a2 * outR;

	if (inR == 0.0f) {
		f->v1R = next_v1R * leakage;
		f->v2R = next_v2R * leakage;

		if (fabsf(f->v1R) < 1e-12f) {
			f->v1R = 0.0f; f->v2R = 0.0f; outR = 0.0f;
		}
	} else {
		f->v1R = next_v1R;
		f->v2R = next_v2R;
	}
	*r = outR;
}

/**
 * @brief Обработка одной полосы методом State Variable Filter
 * f - указатель на структуру фильтра
 * l - левый канал
 * r - правый канал
 */
void process_svf(BandFilter* f, float* l, float* r) {
	const float inL = *l;
	const float inR = *r;
	const float a1 = f->svf_a1;
	const float a2 = f->svf_a2;
	const float a3 = f->svf_a3;
	const float mg = f->svf_m_gain;
	const float leakage = 0.99f;

	/* --- Левый канал --- */
	float v3L = inL - f->v2L;
	float v1L_step = a1 * f->v1L + a2 * v3L;
	float v2L_step = f->v2L + a2 * f->v1L + a3 * v3L;

	/* Правильная сумма для Peak EQ: вход + (gain-1) * bandpass */
	*l = inL + mg * v1L_step;

	/* Обновление состояний по методу трапеций */
	float next_v1L = 2.0f * v1L_step - f->v1L;
	float next_v2L = 2.0f * v2L_step - f->v2L;

	if (inL == 0.0f) {
		f->v1L = next_v1L * leakage;
		f->v2L = next_v2L * leakage;
		if (fabsf(f->v1L) < 1e-12f) { f->v1L = 0.0f; f->v2L = 0.0f; *l = 0.0f; }
	} else {
		f->v1L = next_v1L;
		f->v2L = next_v2L;
	}

	/* --- Правый канал --- */
	float v3R = inR - f->v2R;
	float v1R_step = a1 * f->v1R + a2 * v3R;
	float v2R_step = f->v2R + a2 * f->v1R + a3 * v3R;

	*r = inR + mg * v1R_step;

	float next_v1R = 2.0f * v1R_step - f->v1R;
	float next_v2R = 2.0f * v2R_step - f->v2R;

	if (inR == 0.0f) {
		f->v1R = next_v1R * leakage;
		f->v2R = next_v2R * leakage;
		if (fabsf(f->v1R) < 1e-12f) { f->v1R = 0.0f; f->v2R = 0.0f; *r = 0.0f; }
	} else {
		f->v1R = next_v1R;
		f->v2R = next_v2R;
	}
}


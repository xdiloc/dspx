#ifndef FILTER2_H
#define FILTER2_H

#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846f
#endif

/**
 * @brief Макрос для защиты от денормализации (Anti-Denormal)
 */
#define CLEAN(v) (fabsf(v) < 1e-15f ? 0.0f : v)

typedef enum {
	FORM_DF2 = 0,
	FORM_TDF2 = 1,
	FORM_SVF = 2
} FilterForm;

typedef enum {
	BT_LOWSHELF,
	BT_PEAKING,
	BT_HIGHSHELF
} BandType;

typedef struct {
	float b0, b1, b2, a1, a2;
	float svf_a1, svf_a2, svf_a3, svf_m_gain;
	float v1L, v2L, v1R, v2R;
	float freq;
	float last_gain;
	BandType type;
} BandFilter;

/**
 * @brief Расчет стандартных коэффициентов биквадрата
 * f - указатель на структуру фильтра
 * gain_db - усиление полосы в децибелах
 * sr - частота дискретизации
 */
void compute_coeffs_biquad(BandFilter* f, float gain_db, float sr);

/**
 * @brief Расчет специфичных коэффициентов для SVF
 * f - указатель на структуру фильтра
 * gain_db - усиление полосы в децибелах
 * sr - частота дискретизации
 */
void compute_coeffs_svf(BandFilter* f, float gain_db, float sr);

/**
 * @brief Обработка SVF
 * f - указатель на структуру фильтра
 * l - левый канал
 * r - правый канал
 */
void process_svf(BandFilter* f, float* l, float* r);

/**
 * @brief Обработка TDF2
 * f - указатель на структуру фильтра
 * l - левый канал
 * r - правый канал
 */
void process_tdf2(BandFilter* f, float* l, float* r);

/**
 * @brief Обработка DF2
 * f - указатель на структуру фильтра
 * l - левый канал
 * r - правый канал
 */
void process_df2(BandFilter* f, float* l, float* r);

#endif

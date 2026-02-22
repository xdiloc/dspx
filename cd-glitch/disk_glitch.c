#include <lv2/core/lv2.h>
#include <stdlib.h>
#include <stdint.h>

#define GLITCH_MODE_PORT     0
#define GLITCH_WAIT_MIN_PORT 1
#define GLITCH_WAIT_MAX_PORT 2
#define GLITCH_PROB_PORT     3
#define GLITCH_INPUT_L       4
#define GLITCH_INPUT_R       5
#define GLITCH_OUTPUT_L      6
#define GLITCH_OUTPUT_R      7

/**
 * @brief Структура плагина DiskGlitch
 * @param bufferL, bufferR Кольцевые буферы (аллокация только при создании)
 * @param seed Состояние PRNG Xorshift
 */
typedef struct {
	float* __restrict__ bufferL;
	float* __restrict__ bufferR;
	uint32_t seed;
	int32_t activeMode;
	int32_t offset;
	int32_t counter;
	int32_t timer;
	int32_t wait;
	int32_t nextTrig;
	int32_t sampleRate;
	int32_t maxBuffer;
	int32_t frameSize;

	const float* p_mode;
	const float* p_wait_min;
	const float* p_wait_max;
	const float* p_prob;
	const float* p_in_l;
	const float* p_in_r;
	float* p_out_l;
	float* p_out_r;
} DiskGlitch;

/**
 * @brief Создание экземпляра плагина
 */
static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
	DiskGlitch* m = (DiskGlitch*)malloc(sizeof(DiskGlitch));
	if (m) {
		m->sampleRate = (int32_t)rate;
		m->maxBuffer = m->sampleRate / 10; 
		m->frameSize = m->sampleRate / 75; 
		m->bufferL = (float*)calloc(m->maxBuffer, sizeof(float));
		m->bufferR = (float*)calloc(m->maxBuffer, sizeof(float));
		m->seed = 12345;
		m->counter = 0;
		m->timer = 0;
		m->wait = 0;
		m->nextTrig = m->sampleRate;
		m->activeMode = 0;
	}
	return (LV2_Handle)m;
}

/**
 * @brief Привязка портов
 */
static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
	DiskGlitch* m = (DiskGlitch*)instance;
	switch (port) {
		case GLITCH_MODE_PORT:     m->p_mode = (const float*)data; break;
		case GLITCH_WAIT_MIN_PORT: m->p_wait_min = (const float*)data; break;
		case GLITCH_WAIT_MAX_PORT: m->p_wait_max = (const float*)data; break;
		case GLITCH_PROB_PORT:     m->p_prob = (const float*)data; break;
		case GLITCH_INPUT_L:       m->p_in_l = (const float*)data; break;
		case GLITCH_INPUT_R:       m->p_in_r = (const float*)data; break;
		case GLITCH_OUTPUT_L:      m->p_out_l = (float*)data; break;
		case GLITCH_OUTPUT_R:      m->p_out_r = (float*)data; break;
	}
}

/**
 * @brief Обработка (Hot Path)
 * Оптимизировано под Zen 2 (AVX2): Branchless логика и минимизация ветвлений в цикле.
 */
static void run(LV2_Handle instance, uint32_t sample_count) {
	DiskGlitch* m = (DiskGlitch*)instance;
	uint32_t i;
	int32_t cIdx, currIdx, readIdx, loopPos, waitMin, waitMax, diff, selMode;
	float l, r, click, noise;
	uint32_t probTh;
	
	/* Кэширование указателей буферов (Strength Reduction) */
	float* __restrict__ bL = m->bufferL;
	float* __restrict__ bR = m->bufferR;
	const float* __restrict__ inL = m->p_in_l;
	const float* __restrict__ inR = m->p_in_r;
	float* __restrict__ outL = m->p_out_l;
	float* __restrict__ outR = m->p_out_r;

	waitMin = (int32_t)(*m->p_wait_min * m->sampleRate);
	waitMax = (int32_t)(*m->p_wait_max * m->sampleRate);
	probTh = (uint32_t)(*m->p_prob * 4294967295.0f);
	selMode = (int32_t)*m->p_mode;

	diff = waitMax - waitMin;
	diff = (diff < 1) ? 1 : diff;

	for (i = 0; i < sample_count; ++i) {
		l = inL[i];
		r = inR[i];

		/* Xorshift: Branchless PRNG */
		m->seed ^= m->seed << 13;
		m->seed ^= m->seed >> 17;
		m->seed ^= m->seed << 5;

		cIdx = m->counter;
		currIdx = cIdx % m->maxBuffer;

		if (m->timer <= 0) {
			bL[currIdx] = l;
			bR[currIdx] = r;
			m->wait++;

			if (m->wait >= m->nextTrig) {
				m->wait = 0;
				m->nextTrig = waitMin + (int32_t)(m->seed % (uint32_t)diff);
				m->nextTrig = (m->nextTrig < 1) ? 1 : m->nextTrig;
				m->activeMode = (selMode == 2) ? (int32_t)(m->seed % 2) : selMode;
				m->offset = cIdx;
				m->timer = (m->activeMode == 0) ? 
					(m->frameSize * (4 + (m->seed % 64))) : 
					(m->maxBuffer * 2);
			}
		} else {
			m->timer--;
			/* Branchless-style выбор индекса чтения */
			loopPos = cIdx % m->frameSize;
			readIdx = (m->activeMode == 0) ? 
				((cIdx - m->frameSize + loopPos) % m->maxBuffer) :
				((cIdx - m->offset) % m->maxBuffer);
			
			/* Коррекция отрицательного индекса без if */
			readIdx += (readIdx >> 31) & m->maxBuffer;
			
			l = bL[readIdx];
			r = bR[readIdx];

			/* Эффекты: щелчки и шум через Branchless выбор */
			click = (float)((m->activeMode == 0 && loopPos == 0) || (m->activeMode != 0 && (cIdx % m->frameSize) == 0)) * 0.08f;
			noise = (float)(m->activeMode != 0 && m->seed > 3435973836U) * ((float)(m->seed % 1000) * 0.00002f);
			
			l += noise + click; 
			r -= (noise + click);

			if (m->timer <= 0 && m->seed < probTh) {
				m->timer = (m->activeMode == 0) ? (m->frameSize * (2 + (m->seed % 8))) : (m->maxBuffer * 2);
			}
			
			/* Если режим 0, записываем вход в буфер даже в глитче (Laser Skip) */
			if (m->activeMode == 0) {
				bL[currIdx] = inL[i];
				bR[currIdx] = inR[i];
			}
		}

		outL[i] = l;
		outR[i] = r;
		m->counter = cIdx + 1;
	}
}

/**
 * @brief Удаление плагина
 */
static void cleanup(LV2_Handle instance) {
	DiskGlitch* m = (DiskGlitch*)instance;
	if (m) {
		free(m->bufferL);
		free(m->bufferR);
		free(m);
	}
}

static const LV2_Descriptor descriptor = {
	"https://xdiloc.github.io/dspx/disk-glitch",
	instantiate,
	connect_port,
	NULL, run, NULL, cleanup, NULL
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
	return (index == 0) ? &descriptor : NULL;
}

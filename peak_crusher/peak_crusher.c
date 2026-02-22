#include <lv2/core/lv2.h>
#include <stdlib.h>
#include <math.h>

#define PORT_THRESH 0
#define PORT_IN_L   1
#define PORT_IN_R   2
#define PORT_OUT_L  3
#define PORT_OUT_R  4

/* Таблица на 601 значение (от 0.0 до -60.0 dB с шагом 0.1) */
#define LUT_SIZE 601

/**
 * @brief Структура плагина Peak Crusher
 * @param ports массив указателей на порты
 * @param lut таблица предварительно рассчитанных коэффициентов
 */
typedef struct {
	const float* ports[5];
	float lut[LUT_SIZE];
} PeakCrusher;

/**
 * @brief Инициализация плагина. Расчет LUT.
 * @param descriptor Дескриптор
 * @param rate Частота дискретизации
 * @param bundle_path Путь
 * @param features Фичи
 */
static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
	PeakCrusher* m;
	/* Senior-style: выравнивание по кэш-линии (64 байта) для Zen 2 */
	if (posix_memalign((void**)&m, 64, sizeof(PeakCrusher)) != 0) {
		return NULL;
	}

	/* Look-up Table: убираем powf из run() */
	for (int i = 0; i < LUT_SIZE; i++) {
		float db = -(float)i * 0.1f;
		m->lut[i] = powf(10.0f, db * 0.05f);
	}

	return (LV2_Handle)m;
}

/**
 * @brief Привязка портов
 * @param instance Экземпляр
 * @param port Индекс порта
 * @param data Данные
 */
static void connect_port(LV2_Handle instance, uint32_t port, void* data) {
	((PeakCrusher*)instance)->ports[port] = (const float*)data;
}

/**
 * @brief Hot Path: Обработка аудио. Branchless & No-Math.
 * @param instance Экземпляр
 * @param n Количество сэмплов
 */
static void run(LV2_Handle instance, uint32_t n) {
	PeakCrusher* m = (PeakCrusher*)instance;
	
	/* Быстрое получение лимита из таблицы */
	float db_val = *m->ports[PORT_THRESH];
	/* Ограничение индекса и преобразование в целое (Branchless-friendly) */
	int idx = (int)((db_val > 0.0f ? 0.0f : (db_val < -60.0f ? 60.0f : -db_val)) * 10.0f);
	
	const float limit = m->lut[idx];
	const float neg_limit = -limit;

	const float* __restrict__ inL = m->ports[PORT_IN_L];
	const float* __restrict__ inR = m->ports[PORT_IN_R];
	float* __restrict__ outL = (float*)m->ports[PORT_OUT_L];
	float* __restrict__ outR = (float*)m->ports[PORT_OUT_R];

	/* Цикл векторизуется компилятором благодаря __restrict__ и отсутствию вызовов функций */
	for (uint32_t i = 0; i < n; i++) {
		float curL = inL[i];
		float curR = inR[i];

		/* Branchless Clamp */
		outL[i] = (curL > limit) ? limit : ((curL < neg_limit) ? neg_limit : curL);
		outR[i] = (curR > limit) ? limit : ((curR < neg_limit) ? neg_limit : curR);
	}
}

/**
 * @brief Очистка памяти
 * @param instance Экземпляр
 */
static void cleanup(LV2_Handle instance) {
	free(instance);
}

static const LV2_Descriptor descriptor = {
	"https://xdiloc.github.io/dspx/peak-crusher",
	instantiate, connect_port, NULL, run, NULL, cleanup, NULL
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
	return (index == 0) ? &descriptor : NULL;
}

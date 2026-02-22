#include <lv2/core/lv2.h>
#include <stdlib.h>
#include <math.h>

#define PORT_WIDTH  0
#define PORT_IN_L   1
#define PORT_IN_R   2
#define PORT_OUT_L  3
#define PORT_OUT_R  4

/**
 * @brief Структура плагина Stereo Expander
 * @param ports массив указателей на порты
 */
typedef struct {
	const float* ports[5];
} StereoExpander;

/**
 * @brief Инициализация плагина.
 * @param descriptor Дескриптор
 * @param rate Частота дискретизации
 * @param bundle_path Путь
 * @param features Фичи
 */
static LV2_Handle instantiate(const LV2_Descriptor* descriptor, double rate, const char* bundle_path, const LV2_Feature* const* features) {
	StereoExpander* m;
	/* Senior-style: выравнивание по кэш-линии (64 байта) */
	if (posix_memalign((void**)&m, 64, sizeof(StereoExpander)) != 0) {
		return NULL;
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
	((StereoExpander*)instance)->ports[port] = (const float*)data;
}

/**
 * @brief Hot Path: Стерео расширение с LUT-компенсацией.
 * @param instance Экземпляр
 * @param n Количество сэмплов
 */
static void run(LV2_Handle instance, uint32_t n) {
	StereoExpander* m = (StereoExpander*)instance;
	
	const float width = *m->ports[PORT_WIDTH];
	const float* __restrict__ inL = m->ports[PORT_IN_L];
	const float* __restrict__ inR = m->ports[PORT_IN_R];
	float* __restrict__ outL = (float*)m->ports[PORT_OUT_L];
	float* __restrict__ outR = (float*)m->ports[PORT_OUT_R];

	/**
	 * @brief Constant Power Compensation.
	 * При width = 1.0, compensation должно быть 1.0.
	 * Используем аппроксимацию: gain = sqrt(2 / (1 + width^2))
	 * Для Senior-оптимизации вычисляем один раз вне цикла.
	 */
	const float width_sq = width * width;
	const float compensation = sqrtf(2.0f / (1.0f + width_sq));

	for (uint32_t i = 0; i < n; i++) {
		const float curL = inL[i];
		const float curR = inR[i];

		/* M/S Decomposition (Strength Reduction: * 0.5f) */
		float mid = (curL + curR) * 0.5f;
		float side = (curL - curR) * 0.5f;

		/* Apply Width */
		side *= width;

		/* Branchless Soft Saturation */
		const float upper = fmaxf(0.0f, side - 0.8f);
		const float lower = fminf(0.0f, side + 0.8f);
		side = fminf(0.8f, fmaxf(-0.8f, side)) + (upper + lower) * 0.2f;

		/* Reconstruct L/R с Constant Power Gain */
		outL[i] = (mid + side) * compensation;
		outR[i] = (mid - side) * compensation;
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
	"https://xdiloc.github.io/dspx/stereo-expander",
	instantiate, connect_port, NULL, run, NULL, cleanup, NULL
};

LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t index) {
	return (index == 0) ? &descriptor : NULL;
}

#include <lv2/core/lv2.h>
#include <stdlib.h>
#include <stdio.h>
#include <sndfile.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

/* --- КОНФИГУРАЦИЯ --- */
/* Размер внутреннего буфера (степень двойки для быстрой маски) */
#define BUFFER_SIZE 1048576
#define BUFFER_MASK (BUFFER_SIZE - 1)

/* Индексы портов согласно TTL файлу */
#define PORT_ENABLE  0
#define PORT_FORMAT  1
#define PORT_QUALITY 2
#define PORT_IN_L    3
#define PORT_IN_R    4
#define PORT_OUT_L   5
#define PORT_OUT_R   6

/**
 * @brief Структура состояния плагина Audio Recorder
 * @param ports - массив указателей на данные портов LV2
 * @param file - дескриптор открытого аудиофайла libsndfile
 * @param info - структура с параметрами формата файла (SF_INFO)
 * @param pcm_buffer - выровненный кольцевой буфер для хранения 16-bit PCM данных
 * @param write_pos - атомарный индекс для записи данных в буфер (в кадрах)
 * @param read_pos - атомарный индекс для чтения данных из буфера (в кадрах)
 * @param run_thread - флаг управления жизненным циклом фонового потока
 * @param recording - флаг текущего состояния записи (вкл/выкл)
 * @param thread - идентификатор потока записи на диск
 * @param mutex - мьютекс для синхронизации доступа к условной переменной
 * @param cond_data - условная переменная для перевода потока в спящий режим
 * @param threshold - порог накопления данных перед сбросом на диск (в кадрах)
 * @param last_active - предыдущее состояние порта включения записи
 * @param samplerate - сохраненная частота дискретизации для переинициализации SF_INFO
 */
typedef struct {
	const float* ports[7];
	SNDFILE* file;
	SF_INFO      info;
	short* pcm_buffer;
	
	atomic_uint_fast32_t write_pos;
	atomic_uint_fast32_t read_pos;
	atomic_int           run_thread;
	atomic_int           recording;

	pthread_t       thread;
	pthread_mutex_t mutex;
	pthread_cond_t  cond_data;
	
	uint_fast32_t   threshold;
	int             last_active;
	int             samplerate;
} Recorder;

/**
 * @brief Функция фонового рабочего потока для записи данных на диск
 * @param arg - указатель на структуру Recorder
 */
static void* disk_worker(void* arg) {
	Recorder* m = (Recorder*)arg;
	char path[1024];
	char ts[32];
	const char* home = getenv("HOME");
	int current_format = -1;

	while (atomic_load_explicit(&m->run_thread, memory_order_acquire)) {
		pthread_mutex_lock(&m->mutex);
		/* Системный вызов ожидания: поток не потребляет ресурсы процессора */
		pthread_cond_wait(&m->cond_data, &m->mutex);
		pthread_mutex_unlock(&m->mutex);

		uint_fast32_t wp = atomic_load_explicit(&m->write_pos, memory_order_acquire);
		uint_fast32_t rp = atomic_load_explicit(&m->read_pos, memory_order_relaxed);
		uint_fast32_t len = (wp - rp) & BUFFER_MASK;
		
		int active = atomic_load(&m->recording);
		int target_format = (int)(*m->ports[PORT_FORMAT]);

		/* Смена формата на лету: закрываем текущий файл, если формат на порту изменился */
		if (m->file && target_format != current_format) {
			sf_close(m->file);
			m->file = NULL;
		}

		if (len > 0) {
			/* Инициализация нового файла с уникальной временной меткой */
			if (!m->file && active) {
				const char* ext = "wav";
				current_format = target_format;
				
				/* Формирование метки времени для имени файла */
				time_t rawtime = time(NULL);
				struct tm* timeinfo = localtime(&rawtime);
				strftime(ts, sizeof(ts), "%Y%m%d_%H%M%S", timeinfo);
				
				memset(&m->info, 0, sizeof(SF_INFO));
				m->info.samplerate = m->samplerate;
				m->info.channels   = 2;
				m->info.format     = SF_FORMAT_WAV | SF_FORMAT_PCM_16;

				/* Логика выбора кодека */
				if (current_format == 0) {
					m->info.format = SF_FORMAT_OGG | SF_FORMAT_VORBIS;
					ext = "ogg";
				} else if (current_format == 2) {
					m->info.format = SF_FORMAT_FLAC | SF_FORMAT_PCM_16;
					ext = "flac";
				}

				snprintf(path, 1024, "%s/rec_%s.%s", home ? home : "/tmp", ts, ext);
				m->file = sf_open(path, SFM_WRITE, &m->info);
				
				if (m->file) {
					double q = (double)(*m->ports[PORT_QUALITY]);
					/* Применение качества для OGG */
					if ((m->info.format & SF_FORMAT_TYPEMASK) == SF_FORMAT_OGG)
						sf_command(m->file, SFC_SET_VBR_ENCODING_QUALITY, &q, sizeof(double));
				}
			}

			if (m->file) {
				/* Запись данных из кольцевого буфера на диск */
				uint_fast32_t chunk = (BUFFER_SIZE - rp) < len ? (BUFFER_SIZE - rp) : len;
				sf_writef_short(m->file, m->pcm_buffer + (rp << 1), chunk);
				atomic_store_explicit(&m->read_pos, (rp + chunk) & BUFFER_MASK, memory_order_release);
			}
		} 
		
		/* Остановка записи: закрываем файл и сбрасываем состояние формата */
		if (!active && m->file) {
			sf_close(m->file);
			m->file = NULL;
			current_format = -1;
		}
	}
	return NULL;
}

/**
 * @brief Создание и инициализация экземпляра плагина
 * @param d - дескриптор плагина
 * @param r - частота дискретизации
 * @param p - путь к бандлу
 * @param f - фичи LV2
 */
static LV2_Handle instantiate(const LV2_Descriptor* d, double r, const char* p, const LV2_Feature* const* f) {
	Recorder* m = (Recorder*)calloc(1, sizeof(Recorder));
	if (!m) return NULL;

	m->samplerate      = (int)r;
	m->threshold       = (uint_fast32_t)r * 5; 

	/* Zero-allocation буфер с выравниванием */
	if (posix_memalign((void**)&m->pcm_buffer, 64, sizeof(short) * BUFFER_SIZE * 2) != 0) {
		free(m);
		return NULL;
	}
	
	atomic_init(&m->run_thread, 1);
	atomic_init(&m->recording, 0);
	pthread_mutex_init(&m->mutex, NULL);
	pthread_cond_init(&m->cond_data, NULL);
	pthread_create(&m->thread, NULL, disk_worker, m);

	return (LV2_Handle)m;
}

/**
 * @brief Подключение портов плагина
 * @param h - экземпляр плагина
 * @param port - индекс порта
 * @param data - указатель на данные
 */
static void connect_port(LV2_Handle h, uint32_t port, void* data) {
	((Recorder*)h)->ports[port] = (const float*)data;
}

/**
 * @brief Основной цикл обработки аудио (Hot Path)
 * @param h - экземпляр плагина
 * @param n - количество кадров
 */
static void run(LV2_Handle h, uint32_t n) {
	Recorder* m = (Recorder*)h;
	const float* inL = m->ports[PORT_IN_L];
	const float* inR = m->ports[PORT_IN_R];
	float* outL = (float*)m->ports[PORT_OUT_L];
	float* outR = (float*)m->ports[PORT_OUT_R];
	
	/* Прозрачный Bypass */
	for (uint32_t i = 0; i < n; i++) {
		outL[i] = inL[i];
		outR[i] = inR[i];
	}

	int active = (*m->ports[PORT_ENABLE] > 0.5f);
	atomic_store_explicit(&m->recording, active, memory_order_relaxed);

	if (active) {
		uint_fast32_t wp = atomic_load_explicit(&m->write_pos, memory_order_relaxed);
		for (uint32_t i = 0; i < n; i++) {
			float sL = inL[i] * 32767.0f;
			float sR = inR[i] * 32767.0f;

			/* Branchless clipping */
			sL = (sL > 32767.0f) ? 32767.0f : (sL < -32768.0f ? -32768.0f : sL);
			sR = (sR > 32767.0f) ? 32767.0f : (sR < -32768.0f ? -32768.0f : sR);

			m->pcm_buffer[wp << 1]       = (short)sL;
			m->pcm_buffer[(wp << 1) | 1] = (short)sR;
			wp = (wp + 1) & BUFFER_MASK;
		}
		atomic_store_explicit(&m->write_pos, wp, memory_order_release);

		uint_fast32_t rp = atomic_load_explicit(&m->read_pos, memory_order_acquire);
		if (((wp - rp) & BUFFER_MASK) >= m->threshold) {
			pthread_cond_signal(&m->cond_data);
		}
	} else if (m->last_active) {
		pthread_cond_signal(&m->cond_data);
	}
	m->last_active = active;
}

/**
 * @brief Очистка ресурсов
 * @param h - экземпляр плагина
 */
static void cleanup(LV2_Handle h) {
	Recorder* m = (Recorder*)h;
	if (!m) return;

	atomic_store(&m->run_thread, 0);
	pthread_cond_signal(&m->cond_data);
	pthread_join(m->thread, NULL);
	
	if (m->file) sf_close(m->file);
	pthread_mutex_destroy(&m->mutex);
	pthread_cond_destroy(&m->cond_data);
	free(m->pcm_buffer);
	free(m);
}

static const LV2_Descriptor desc = {
	"https://xdiloc.github.io/dspx/audio-recorder",
	instantiate, connect_port, NULL, run, NULL, cleanup, NULL
};

/**
 * @brief Точка входа в библиотеку LV2
 */
LV2_SYMBOL_EXPORT const LV2_Descriptor* lv2_descriptor(uint32_t i) {
	return (i == 0) ? &desc : NULL;
}

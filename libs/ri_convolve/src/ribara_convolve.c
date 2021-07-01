#include "ribara_convolve.h"

#include <assert.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "ri_ring_buffer.h"
#include "ri_convolve.h"
#include "ri_karatsuba.h"
#include "ri_fft_convolve.h"

/* メモリアラインメント */
#define RIBARACONVOLVE_ALIGNMENT 16
/* 時間領域畳み込みの係数長 */
#define RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS 1024
/* 最小値の取得 */
#define MIN(a, b) (((a) < (b)) ? (a) : (b))
/* 最大値の取得 */
#define MAX(a, b) (((a) > (b)) ? (a) : (b))
/* nの倍数切り上げ */
#define ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))

struct RIbaraConvolve {
    const struct RIConvolveInterface *time_conv_if; /* 時間領域畳み込みモジュールインターフェース	*/
    const struct RIConvolveInterface *freq_conv_if; /* 周波数領域畳み込みモジュールインターフェース */
    void *time_conv_obj; /* 時間領域畳み込みモジュールオブジェクト本体 */
    void *freq_conv_obj; /* 時間領域畳み込みモジュールオブジェクト本体 */
    uint8_t use_freq_conv; /* 周波数畳み込みを行うか？ */
    struct RIRingBuffer *input_buffer; /* 入力遅延バッファ */			
    float *output_buffer; /* 出力データバッファ */		
    uint32_t max_num_input_samples; /* 最大入力サンプル数 */
};

/* ワークサイズ取得 */
static int32_t RIbaraConvolve_CalculateWorkSize(const struct RIConvolveConfig *config);
/* インスタンス生成 */
static void* RIbaraConvolve_Create(const struct RIConvolveConfig *config, void *work, int32_t work_size);
/* インスタンス破棄 */
static void	RIbaraConvolve_Destroy(void *obj);
/* 内部状態リセット */
static void	RIbaraConvolve_Reset(void *obj);
/* 係数セット */
static void	RIbaraConvolve_SetCoefficients(void *obj, const float *coefficients, uint32_t num_coefficients);
/* 畳み込み */
static void	RIbaraConvolve_Convolve(void *obj, const float *input, float *output, uint32_t num_samples);
/* レイテンシ取得 */
static int32_t RIbaraConvolve_GetLatencyNumSamples(void *obj);

/* インターフェース */
static const struct RIConvolveInterface st_ribara_convolve_if = {
    RIbaraConvolve_CalculateWorkSize,
    RIbaraConvolve_Create,
    RIbaraConvolve_Destroy,
    RIbaraConvolve_Reset,
    RIbaraConvolve_SetCoefficients,
    RIbaraConvolve_Convolve,
    RIbaraConvolve_GetLatencyNumSamples,
};

/* インターフェース取得 */
const struct RIConvolveInterface* RIbaraConvolve_GetInterface(void)
{
    return &st_ribara_convolve_if;
}

/* ワークサイズ計算 */
static int32_t RIbaraConvolve_CalculateWorkSize(const struct RIConvolveConfig *config)
{
    int32_t	time_conv_size, freq_conv_size, delay_buffer_size, work_size;
    struct RIRingBufferConfig buffer_config;
    struct RIConvolveConfig conv_config;
    const struct RIConvolveInterface *time_conv_if = RIKaratsuba_GetInterface();
    const struct RIConvolveInterface *freq_conv_if = RIFFTConvolve_GetInterface();

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }

    /* 最大入力サンプル数は共通 */
    conv_config.max_num_input_samples = config->max_num_input_samples;

    /* 時間領域畳み込みモジュール分 */
    conv_config.max_num_coefficients = RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS;
    if ((time_conv_size = time_conv_if->CalculateWorkSize(&conv_config)) < 0) {
        return -1;
    }

    /* 周波数領域畳み込みモジュール分 */
    conv_config.max_num_coefficients = config->max_num_coefficients;
    if ((freq_conv_size = freq_conv_if->CalculateWorkSize(&conv_config)) < 0) {
        return -1;
    }

    /* ディレイバッファ分 */
    buffer_config.max_size = sizeof(float) * (config->max_num_input_samples + RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS);
    buffer_config.max_required_size = sizeof(float) * config->max_num_input_samples;
    delay_buffer_size = RIRingBuffer_CalculateWorkSize(&buffer_config);

    work_size = sizeof(struct RIbaraConvolve) + RIBARACONVOLVE_ALIGNMENT;
    work_size += time_conv_size;
    work_size += freq_conv_size;
    work_size += delay_buffer_size;
    work_size += sizeof(float) * config->max_num_input_samples + RIBARACONVOLVE_ALIGNMENT;

    return work_size;
}

/* インスタンス生成 */
static void* RIbaraConvolve_Create(const struct RIConvolveConfig *config, void *work, int32_t work_size)
{
    struct RIbaraConvolve *conv;
    uint8_t *work_ptr = (uint8_t *)work;
    struct RIConvolveConfig conv_config;
    struct RIRingBufferConfig buffer_config;
    int32_t tmp_work_size;

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < RIbaraConvolve_CalculateWorkSize(config))) {
        return NULL;
    }

    work_ptr = (uint8_t *)work;

    /* 構造体配置 */
    work_ptr = (uint8_t *)ROUNDUP((uintptr_t)work_ptr, RIBARACONVOLVE_ALIGNMENT);
    conv = (struct RIbaraConvolve *)work_ptr;
    conv->time_conv_if = RIKaratsuba_GetInterface();
    conv->freq_conv_if = RIFFTConvolve_GetInterface();
    conv->max_num_input_samples = config->max_num_input_samples;
    work_ptr += sizeof(struct RIbaraConvolve);

    /* 共通のパラメータ設定項目 */
    conv_config.max_num_input_samples = config->max_num_input_samples;

    /* 時間領域畳み込みモジュール */
    conv_config.max_num_coefficients = RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS;
    if ((tmp_work_size = conv->time_conv_if->CalculateWorkSize(&conv_config)) < 0) {
        return NULL;
    }
    conv->time_conv_obj = conv->time_conv_if->Create(&conv_config, work_ptr, tmp_work_size);
    work_ptr += tmp_work_size;

    /* 周波数領域畳み込みモジュール */
    conv_config.max_num_coefficients = config->max_num_coefficients;
    if ((tmp_work_size = conv->freq_conv_if->CalculateWorkSize(&conv_config)) < 0) {
        return NULL;
    }
    conv->freq_conv_obj = conv->freq_conv_if->Create(&conv_config, work_ptr, tmp_work_size);
    work_ptr += tmp_work_size;

    /* ディレイバッファ */
    buffer_config.max_size = sizeof(float) * (config->max_num_input_samples + RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS);
    buffer_config.max_required_size = sizeof(float) * config->max_num_input_samples;
    if ((tmp_work_size = RIRingBuffer_CalculateWorkSize(&buffer_config)) < 0) {
        return NULL;
    }
    conv->input_buffer = RIRingBuffer_Create(&buffer_config, work_ptr, tmp_work_size);
    work_ptr += tmp_work_size;

    /* 出力データバッファ */
    conv->output_buffer	= (float *)ROUNDUP((uintptr_t)work_ptr, RIBARACONVOLVE_ALIGNMENT);
    work_ptr += sizeof(float) * config->max_num_input_samples;

    return conv;
}

/* インスタンス破棄 */
static void RIbaraConvolve_Destroy(void *obj)
{
    struct RIbaraConvolve *conv = (struct RIbaraConvolve *)obj;

    if (conv != NULL) {
        /* リングバッファ破棄 */
        RIRingBuffer_Destroy(conv->input_buffer);
        /* 各畳み込みモジュールの破棄 */
        conv->time_conv_if->Destroy(conv->time_conv_obj);
        conv->freq_conv_if->Destroy(conv->freq_conv_obj);
    }
}

/* 係数セット */
static void RIbaraConvolve_SetCoefficients(void *obj, const float *coefficients, uint32_t num_coefficients)
{
    struct RIbaraConvolve *conv = (struct RIbaraConvolve *)obj;

    if (num_coefficients > RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS) {
        conv->use_freq_conv = 1;
        /* 先頭分を時間領域畳み込みモジュールにセット */
        conv->time_conv_if->SetCoefficients(conv->time_conv_obj, coefficients, RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS);
        /* 後ろは周波数領域畳み込みモジュールにセット */
        conv->freq_conv_if->SetCoefficients(conv->freq_conv_obj,
                &coefficients[RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS],
                num_coefficients - RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS);
    } else {
        conv->use_freq_conv = 0;
        /* 時間領域畳み込みモジュールで十分 */
        conv->time_conv_if->SetCoefficients(conv->time_conv_obj, coefficients, num_coefficients);
    }

    /* 内部状態をリセット（前の係数の影響をクリア） */
    RIbaraConvolve_Reset(conv);
}

/* 畳み込み計算 */
static void RIbaraConvolve_Convolve(void *obj, const float *input, float *output, uint32_t num_samples)
{
    struct RIbaraConvolve *conv = (struct RIbaraConvolve *)obj;

    /* 先頭分を時間領域で畳み込み */
    conv->time_conv_if->Convolve(conv->time_conv_obj, input, output, num_samples);

    if (conv->use_freq_conv == 1) {
        void *buffer_ptr;
        uint32_t smpl;
        const uint32_t sample_size = sizeof(float) * num_samples;

        /* ディレイバッファに入力 */
        RIRingBuffer_Put(conv->input_buffer, input, sample_size);
        /* ディレイバッファから遅延入力を取得/畳み込み */
        RIRingBuffer_Get(conv->input_buffer, &buffer_ptr, sample_size);
        conv->freq_conv_if->Convolve(conv->freq_conv_obj, (const float *)buffer_ptr, conv->output_buffer, num_samples);

        /* 時間領域の結果とミックス */
        for (smpl = 0; smpl < num_samples; smpl++) {
            output[smpl] += conv->output_buffer[smpl];
        }
    }
}

/* 内部状態リセット */
static void RIbaraConvolve_Reset(void *obj)
{
    struct RIbaraConvolve *conv = (struct RIbaraConvolve *)obj;
    int32_t smpl, num_input_delay;

    /* 各畳み込みモジュールのリセット */
    conv->time_conv_if->Reset(conv->time_conv_obj);
    conv->freq_conv_if->Reset(conv->freq_conv_obj);

    /* ディレイバッファのリセット */
    RIRingBuffer_Clear(conv->input_buffer);

    /* 時間領域フィルタ係数分の遅延を実現するため、レイテンシで減じた分だけの無音を挿入 */
    num_input_delay = (int32_t)RIBARACONVOLVE_NUM_TIMEDOMAIN_COEFFICIENTS - conv->freq_conv_if->GetLatencyNumSamples(conv->freq_conv_obj);
    assert(num_input_delay >= 0);
    memset(conv->output_buffer, 0, sizeof(float) * conv->max_num_input_samples);
    smpl = 0;
    while (smpl < num_input_delay) {
        const uint32_t num_samples = MIN(conv->max_num_input_samples, (uint32_t)(num_input_delay - smpl));
        RIRingBuffer_Put(conv->input_buffer, conv->output_buffer, sizeof(float) * num_samples);
        smpl += num_samples;
    }
}

/* レイテンシーの取得 */
static int32_t RIbaraConvolve_GetLatencyNumSamples(void *obj)
{
    (void)obj;
    return 0;
}

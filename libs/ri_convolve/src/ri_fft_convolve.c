#include "ri_fft_convolve.h"

#include <string.h>
#include <math.h>
#include <assert.h>

#include "ri_convolve.h"
#include "ri_fft.h"
#include "ri_ring_buffer.h"

/* FFT点数 */
#define RIFFTCONVOLVE_FFT_SIZE 2048
/* メモリアラインメント */
#define RIFFTCONVOLVE_ALIGNMENT 16
/* ある整数が2の冪乗か判定. 0:2の冪乗ではない, それ以外:2の冪乗 */
#define IS_POWER_OF_2(x) (!((x) & ((x) - 1))) 
/* 最大値を取得 */
#define MAX(a,b) (((a) > (b)) ? (a) : (b))
/* 最小値を取得 */
#define MIN(a,b) (((a) < (b)) ? (a) : (b))
/* nの倍数切り上げ */
#define ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))

/* FFT畳み込み構造体 */
struct RIFFTConvolve {
    uint32_t fft_size; /* FFT点数 */
    uint32_t partition_size; /* 係数の分割サイズ: fft_size / 2 が成立 */	
    uint32_t num_coefficients; /* 係数長 */
    uint32_t max_num_coefficients; /* 最大係数長 */
    uint32_t num_partitions; /* 係数の分割数: num_coefficients/partition_size が成立 */
    uint32_t buffer_count; /* 入力バッファサンプル数カウント */
    uint32_t current_part; /* 現在処理中の分割 */
    uint32_t max_num_input_samples;	/* 最大入力サンプル数 */
    float *ir_freq; /* フーリエ変換済みのインパルス応答 */
    struct RIRingBuffer *input_buffer; /* 入力データリングバッファ */
    struct RIRingBuffer *output_buffer; /* 出力データリングバッファ */
    struct RIRingBuffer *freq_buffer; /* 周波数領域に変換したデータバッファ */
    void *freq_buffer_work; /* データバッファのワーク領域先頭ポインタ */
    float *work_buffer[2]; /* 複素数演算バッファ */
    float *comp_muladd_buffer; /* 複素数乗算/加算計算結果バッファ */
};

/* ワークサイズ計算 */
static int32_t RIFFTConvolve_CalculateWorkSize(const struct RIConvolveConfig *config);
/* インスタンス生成 */
static void* RIFFTConvolve_Create(const struct RIConvolveConfig *config, void *work, int32_t work_size);
/* インスタンス破棄 */
static void RIFFTConvolve_Destroy(void *obj);
/* 内部状態リセット */
static void RIFFTConvolve_Reset(void *obj);
/* 係数セット */
static void RIFFTConvolve_SetCoefficients(void *obj, const float *coefficients, uint32_t num_coefficients);
/* ワークサイズ計算 */
static void RIFFTConvolve_Convolve(void *obj, const float *input, float *output, uint32_t num_samples);
/* レイテンシーの取得 */
static int32_t RIFFTConvolve_GetLatencyNumSamples(void *obj);

/* 引数を2の冪乗に切り上げる */
static uint32_t RIFFTConvolve_Roundup2PoweredValue(uint32_t val);
/* srcとcoefを複素乗算し、dstに足し込む */
static void RIFFTConvolve_MulAddSpectrum(
        float *dst, const float *src, const float *coef, uint32_t num_complex);

/* インターフェース */
static const struct RIConvolveInterface st_fft_convolve_if = {
    RIFFTConvolve_CalculateWorkSize,
    RIFFTConvolve_Create,
    RIFFTConvolve_Destroy,
    RIFFTConvolve_Reset,
    RIFFTConvolve_SetCoefficients,
    RIFFTConvolve_Convolve,
    RIFFTConvolve_GetLatencyNumSamples,
};

/* FFTのサイズチェック */
extern char fft_size_check[IS_POWER_OF_2(RIFFTCONVOLVE_FFT_SIZE) ? 1 : -1];

/* インターフェース取得 */
const struct RIConvolveInterface *RIFFTConvolve_GetInterface(void)
{
    return &st_fft_convolve_if;
}

/* ワークサイズ計算 */
static int32_t RIFFTConvolve_CalculateWorkSize(const struct RIConvolveConfig *config)
{
    int32_t work_size;
    uint32_t fft_size, max_fft_size, max_num_partitions;
    int32_t time_buffer_work_size, freq_buffer_work_size;
    struct RIRingBufferConfig buffer_config;

    if (config == NULL) {
        return -1;
    }

    /* FFTサイズ */
    fft_size = RIFFTCONVOLVE_FFT_SIZE;

    /* 最大のFFTサイズを取得 */
    /* 補足）2倍するのは巡回畳み込み対策。FFT畳み込み結果の半分は折り返している。 */
    max_fft_size = MAX(fft_size, 2 * RIFFTConvolve_Roundup2PoweredValue(config->max_num_coefficients));

    /* 最大分割数の計算 */
    max_num_partitions = max_fft_size / fft_size;

    /* 入出力リングバッファの領域計算 */
    buffer_config.max_size = sizeof(float) * (fft_size + config->max_num_input_samples);
    /* FFT点数分、もしくは最大サンプル数分拾ってくる場合がある */
    buffer_config.max_required_size = sizeof(float) * MAX(fft_size, config->max_num_input_samples);
    time_buffer_work_size = RIRingBuffer_CalculateWorkSize(&buffer_config);
    if (time_buffer_work_size < 0) {
        return -1;
    }

    /* 周波数領域に変換したデータのバッファの領域計算 */
    /* 係数設定時に係数長に合わせたサイズのリングバッファを再構築する */
    buffer_config.max_size = sizeof(float) * max_num_partitions * fft_size;
    buffer_config.max_required_size = sizeof(float) * fft_size; 
    freq_buffer_work_size = RIRingBuffer_CalculateWorkSize(&buffer_config);
    if (freq_buffer_work_size < 0) {
        return -1;
    }

    /* ハンドル領域分 */
    work_size = sizeof(struct RIFFTConvolve) + RIFFTCONVOLVE_ALIGNMENT;
    /* フーリエ変換済みの係数領域分 */
    work_size += sizeof(float) * max_num_partitions * fft_size + RIFFTCONVOLVE_ALIGNMENT;
    /* 複素作業領域分 FFT点数分確保 */
    work_size += 2 * (sizeof(float) * fft_size + RIFFTCONVOLVE_ALIGNMENT);
    /* 複素乗算/加算作業領域分 FFT点数分確保 */
    work_size += (sizeof(float) * fft_size + RIFFTCONVOLVE_ALIGNMENT);
    /* 入出力データバッファ分 */
    work_size += 2 * time_buffer_work_size;
    /* 周波数領域に変換したデータのバッファ分 */
    work_size += freq_buffer_work_size;

    return work_size;
}

/* インスタンス生成 */
static void* RIFFTConvolve_Create(const struct RIConvolveConfig *config, void *work, int32_t work_size)
{
    uint8_t *work_ptr = (uint8_t *)work;
    struct RIFFTConvolve* conv;
    uint32_t fft_size, max_fft_size, max_num_partitions;
    int32_t buffer_work_size;
    struct RIRingBufferConfig buffer_config;

    /* 引数チェック */
    if ((config == NULL) || (work == NULL)
            || (work_size < RIFFTConvolve_CalculateWorkSize(config))) {
        return NULL;
    }

    /* FFTサイズ */
    fft_size = RIFFTCONVOLVE_FFT_SIZE;

    /* 最大のFFTサイズを取得 */
    /* 補足）2倍するのは巡回畳み込み対策。FFT畳み込み結果の半分は折り返している。 */
    max_fft_size = MAX(fft_size, 2 * RIFFTConvolve_Roundup2PoweredValue(config->max_num_coefficients));

    /* 最大分割数の計算 */
    max_num_partitions = max_fft_size / fft_size;

    /* 構造体を配置 */
    work_ptr = (uint8_t *)ROUNDUP((uintptr_t)work_ptr, RIFFTCONVOLVE_ALIGNMENT);
    conv = (struct RIFFTConvolve *)work_ptr;
    conv->fft_size = fft_size;
    conv->partition_size = fft_size / 2;
    conv->max_num_coefficients = RIFFTConvolve_Roundup2PoweredValue(config->max_num_coefficients);
    conv->max_num_input_samples = config->max_num_input_samples;
    conv->num_coefficients = fft_size / 2;
    conv->num_partitions = 1;
    work_ptr += sizeof(struct RIFFTConvolve);

    /* 変換済み係数の割り当て */
    work_ptr = (uint8_t *)ROUNDUP((uintptr_t)work_ptr, RIFFTCONVOLVE_ALIGNMENT);
    conv->ir_freq = (float *)work_ptr;
    work_ptr += sizeof(float) * max_num_partitions * fft_size;

    /* 作業領域の割り当て */
    work_ptr = (uint8_t *)ROUNDUP((uintptr_t)work_ptr, RIFFTCONVOLVE_ALIGNMENT);
    conv->work_buffer[0] = (float *)work_ptr;
    work_ptr += sizeof(float) * fft_size;
    work_ptr = (uint8_t *)ROUNDUP((uintptr_t)work_ptr, RIFFTCONVOLVE_ALIGNMENT);
    conv->work_buffer[1] = (float *)work_ptr;
    work_ptr += sizeof(float) * fft_size;
    work_ptr = (uint8_t *)ROUNDUP((uintptr_t)work_ptr, RIFFTCONVOLVE_ALIGNMENT);
    conv->comp_muladd_buffer = (float *)work_ptr;
    work_ptr += sizeof(float) * fft_size;

    /* 入力/出力データバッファ */
    buffer_config.max_size = sizeof(float) * (fft_size + config->max_num_input_samples);
    /* FFT点数分、もしくは最大サンプル数分取得する場合がある */
    buffer_config.max_required_size = sizeof(float) * MAX(fft_size, config->max_num_input_samples);
    buffer_work_size = RIRingBuffer_CalculateWorkSize(&buffer_config);
    if (buffer_work_size < 0) {
        return NULL;
    }
    conv->input_buffer = RIRingBuffer_Create(&buffer_config, work_ptr, buffer_work_size);
    work_ptr += buffer_work_size;
    conv->output_buffer = RIRingBuffer_Create(&buffer_config, work_ptr, buffer_work_size);
    work_ptr += buffer_work_size;

    /* 周波数領域に変換したデータバッファ */
    /* 係数設定時に係数長に合わせたサイズのリングバッファを再構築する */
    buffer_config.max_size = sizeof(float) * max_num_partitions * fft_size;
    buffer_config.max_required_size = sizeof(float) * fft_size; 
    buffer_work_size = RIRingBuffer_CalculateWorkSize(&buffer_config);
    if (buffer_work_size < 0) {
        return NULL;
    }
    conv->freq_buffer_work = work_ptr;
    conv->freq_buffer = RIRingBuffer_Create(&buffer_config, work_ptr, buffer_work_size);
    work_ptr += buffer_work_size;

    /* バッファをリセット */
    RIFFTConvolve_Reset(conv);

    return conv;
}

/* インスタンス破棄 */
static void RIFFTConvolve_Destroy(void *obj)
{
    struct RIFFTConvolve *conv = (struct RIFFTConvolve *)obj;

    if (conv != NULL) {
        /* リングバッファを破棄 */
        RIRingBuffer_Destroy(conv->input_buffer);
        RIRingBuffer_Destroy(conv->output_buffer);
        RIRingBuffer_Destroy(conv->freq_buffer);
    }
}

/* 係数セット */
static void RIFFTConvolve_SetCoefficients(void *obj, const float *coefficients, uint32_t num_coefficients)
{
    uint32_t smpl, i;
    struct RIFFTConvolve *conv = (struct RIFFTConvolve *)obj;
    float norm_factor_inverse;
    struct RIRingBufferConfig buffer_config;
    int32_t buffer_work_size;

    /* 引数チェック */
    assert((obj != NULL) && (coefficients != NULL));

    /* 係数サイズチェック */
    assert(num_coefficients <= conv->max_num_coefficients);

    /* 係数サイズは分割処理単位に切り上げる */
    conv->num_coefficients = ROUNDUP(num_coefficients, conv->partition_size);
    /* 分割数の再計算 */
    conv->num_partitions = conv->num_coefficients / conv->partition_size;

    /* 後半0埋めを行いつつFFT */
    norm_factor_inverse = 2.0f / conv->fft_size;
    for (smpl = 0; smpl < conv->num_coefficients; smpl += conv->partition_size) {
        const uint32_t copy_samples = MIN(conv->partition_size, num_coefficients - smpl);
        /* 一旦0埋め（後半部分の0埋めを兼ねる） */
        memset(conv->work_buffer[0], 0, sizeof(float) * conv->fft_size);
        /* 係数コピー */
        memcpy(conv->work_buffer[0], &coefficients[smpl], sizeof(float) * copy_samples);
        /* 変換前に正規化 */
        for (i = 0; i < copy_samples; i++) {
            conv->work_buffer[0][i] *= norm_factor_inverse;
        }
        /* 係数をFFT */
        RIFFT_RealFFT((int)conv->fft_size, -1, conv->work_buffer[0], conv->work_buffer[1]);
        /* 結果をコピー */
        memcpy(&conv->ir_freq[2 * smpl], conv->work_buffer[0], sizeof(float) * conv->fft_size);
    }

    /* 周波数領域に変換したデータバッファを再構築 */
    RIRingBuffer_Destroy(conv->freq_buffer);
    buffer_config.max_size = sizeof(float) * conv->num_partitions * conv->fft_size;
    buffer_config.max_required_size = sizeof(float) * conv->fft_size; 
    buffer_work_size = RIRingBuffer_CalculateWorkSize(&buffer_config);
    assert(buffer_work_size > 0);
    conv->freq_buffer = RIRingBuffer_Create(&buffer_config, conv->freq_buffer_work, buffer_work_size);
    assert(conv->freq_buffer != NULL);

    /* 内部バッファリセット */
    RIFFTConvolve_Reset(conv);
}

/* 畳み込み計算 */
static void RIFFTConvolve_Convolve(void *obj, const float *input, float *output, uint32_t num_samples)
{
    struct RIFFTConvolve *conv = (struct RIFFTConvolve *)obj;
    const uint32_t input_size = sizeof(float) * num_samples;
    const uint32_t freqbuffer_unit_size = sizeof(float) * conv->fft_size; /* 周波数データバッファの処理単位 */
    void *buffer_ptr;

    /* 引数チェック */
    assert((obj != NULL) && (input != NULL) && (output != NULL));

    /* 入力のバッファリング */
    RIRingBuffer_Put(conv->input_buffer, input, input_size);

    /* バッファサンプル数を増加 */
    conv->buffer_count += num_samples;

    /* FFTするまでのサンプルが溜まっていない時は複素乗算/加算を進める */
    if (conv->buffer_count < conv->fft_size) {
        uint32_t goal_part;

        /* 分割数処理目標値 */
        goal_part = ((conv->num_partitions + 1) * (conv->buffer_count - conv->partition_size)) / conv->partition_size;
        /* 分割数を上限に設ける */
        goal_part = MIN(goal_part, conv->num_partitions);

        /* 周波数領域で複素乗算/加算 */
        for (; conv->current_part < goal_part; conv->current_part++) {
            /* バッファ先頭からは最も古い結果が取れるので、係数末尾から畳み込みを行う */
            const uint32_t part_offset = (conv->num_partitions - conv->current_part) * conv->fft_size;
            /* 周波数バッファを取り出す */
            RIRingBuffer_Get(conv->freq_buffer, &buffer_ptr, freqbuffer_unit_size);
            RIFFTConvolve_MulAddSpectrum(conv->comp_muladd_buffer,
                    (const float *)buffer_ptr, &conv->ir_freq[part_offset], conv->partition_size);
            /* バッファ末尾に再挿入 */
            RIRingBuffer_Put(conv->freq_buffer, buffer_ptr, freqbuffer_unit_size);
        }
    }

    /* FFT点数/2毎にFFT畳み込み処理を実行し、出力バッファに結果を書き出す */
    /* FFT点数/2が入力サンプル数よりも小さい場合があるので、入力サンプル分を消費するまでwhileで回す */
    while (conv->buffer_count >= conv->fft_size) {
        /* 残った分の複素乗算/加算を実行 */
        for (; conv->current_part < conv->num_partitions; conv->current_part++) {
            const uint32_t part_offset = (conv->num_partitions - conv->current_part) * conv->fft_size;
            RIRingBuffer_Get(conv->freq_buffer, &buffer_ptr, freqbuffer_unit_size);
            RIFFTConvolve_MulAddSpectrum(conv->comp_muladd_buffer,
                    (const float *)buffer_ptr, &conv->ir_freq[part_offset], conv->partition_size);
            RIRingBuffer_Put(conv->freq_buffer, buffer_ptr, freqbuffer_unit_size);
        }

        /* 入力バッファからFFTサイズ分データを取り出し */
        /* FFT点数/2だけバッファを進めるため、取り出しサイズは freqbuffer_unit_size / 2 */
        RIRingBuffer_Get(conv->input_buffer, &buffer_ptr, freqbuffer_unit_size / 2);
        memcpy(conv->work_buffer[0], buffer_ptr, freqbuffer_unit_size); /* 注: 取得するのはfreqbuffer_unit_size */

        /* FFT */
        RIFFT_RealFFT((int)conv->fft_size, -1, conv->work_buffer[0], conv->work_buffer[1]);

        /* 結果を周波数バッファに入力（一番古いデータは消去） */
        RIRingBuffer_Get(conv->freq_buffer, &buffer_ptr, freqbuffer_unit_size);
        RIRingBuffer_Put(conv->freq_buffer, conv->work_buffer[0], freqbuffer_unit_size);
        
        /* 係数先頭分を複素乗算/加算 */
        RIFFTConvolve_MulAddSpectrum(conv->comp_muladd_buffer, conv->work_buffer[0], &conv->ir_freq[0], conv->partition_size);

        /* IFFT */
        RIFFT_RealFFT((int)conv->fft_size, 1, conv->comp_muladd_buffer, conv->work_buffer[1]);

        /* 結果を出力バッファに書き出す */
        /* FFT畳み込みで有効なのは結果後半のみ（直線畳み込み）。後半のみ出力バッファに書き出す */
        RIRingBuffer_Put(conv->output_buffer, &conv->comp_muladd_buffer[conv->fft_size / 2], freqbuffer_unit_size / 2);

        /* 複素数乗算/加算結果バッファをクリア */
        memset(conv->comp_muladd_buffer, 0, freqbuffer_unit_size);

        /* バッファデータ数を削減 */
        conv->buffer_count -= conv->fft_size / 2;

        /* 現在処理中の分割をリセット */
        conv->current_part = 1;
    }

    /* 出力バッファから取り出し */
    RIRingBuffer_Get(conv->output_buffer, &buffer_ptr, input_size);
    memcpy(output, buffer_ptr, input_size);
}

/* srcとcoefを複素乗算し、dstに足し込む */
static void RIFFTConvolve_MulAddSpectrum(
        float *dst, const float *src, const float *coef, uint32_t num_complex)
{
    uint32_t cmplx;
    float src_re, src_im, coef_re, coef_im;
    float re, im;

    /* 先頭の1複素数(float配列2要素)は直流成分と最高周波数成分の実部 */
    dst[0] += src[0] * coef[0];
    dst[1] += src[1] * coef[1];

    /* それ以降の要素は複素数が入っている */
    for (cmplx = 1; cmplx < num_complex; cmplx++) {
        src_re = RIFFTCOMPLEX_REAL(src, cmplx); src_im = RIFFTCOMPLEX_IMAG(src, cmplx);
        coef_re = RIFFTCOMPLEX_REAL(coef, cmplx); coef_im = RIFFTCOMPLEX_IMAG(coef, cmplx);
        /* 複素乗算 */
        re = src_re * coef_re - src_im * coef_im;
        im = src_im * coef_re + src_re * coef_im;
        /* バッファに加算 */
        RIFFTCOMPLEX_REAL(dst, cmplx) += re;
        RIFFTCOMPLEX_IMAG(dst, cmplx) += im;
    }
}

/* 内部状態リセット */
static void RIFFTConvolve_Reset(void *obj)
{
    uint32_t part;
    struct RIFFTConvolve *conv = (struct RIFFTConvolve *)obj;
    const uint32_t fft_buffer_size = sizeof(float) * conv->fft_size;

    /* 作業領域をクリア */
    memset(conv->work_buffer[0], 0, fft_buffer_size);
    memset(conv->work_buffer[1], 0, fft_buffer_size);
    memset(conv->comp_muladd_buffer, 0, fft_buffer_size);

    /* リングバッファをリセット */
    RIRingBuffer_Clear(conv->input_buffer);
    RIRingBuffer_Clear(conv->output_buffer);
    RIRingBuffer_Clear(conv->freq_buffer);

    /* リングバッファに無音を挿入 */
    /* 補足）最初のFFT点数/2の分はFFTを行うまで出力できないため、無音を入れておく */
    RIRingBuffer_Put(conv->input_buffer,  conv->work_buffer[0], fft_buffer_size / 2);
    RIRingBuffer_Put(conv->output_buffer, conv->work_buffer[0], fft_buffer_size / 2);

    /* 周波数領域に変換したデータに0を詰める */
    /* 補足）初回の分割での入力がバッファ末尾に入る様に、分割数-1までを全て0で埋める */
    for (part = 0; part < conv->num_partitions - 1; part++) {
        RIRingBuffer_Put(conv->freq_buffer, conv->work_buffer[0], fft_buffer_size);
    }

    /* 入力カウントをリセット */
    conv->buffer_count = conv->fft_size / 2;

    /* 現在処理中の分割をリセット */
    conv->current_part = 1;
}

/* レイテンシーの取得 */
static int32_t RIFFTConvolve_GetLatencyNumSamples(void *obj)
{
    struct RIFFTConvolve *conv = (struct RIFFTConvolve *)obj;

    /* 分割サイズ(=FFT点数/2)分遅れる */
    return (int32_t)conv->partition_size;
}

/* 2の冪乗に切り上げ */
static uint32_t RIFFTConvolve_Roundup2PoweredValue(uint32_t val)
{
    val--;
    val |= val >> 1;
    val |= val >> 2;
    val |= val >> 4;
    val |= val >> 8;
    val |= val >> 16;
    val++;

    return val;
}

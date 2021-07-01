#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

#define MIN(a, b) (((a) < (b)) ? (a) : (b))
#define FLOAT_EPSILON 0.001f /* 許容誤差(振幅絶対値) */
#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* テスト対象のモジュール */
#include "../../libs/ri_convolve/include/ri_karatsuba.h"
#include "../../libs/ri_convolve/include/ribara_convolve.h"
#include "../../libs/ri_convolve/include/ri_fft_convolve.h"

/* 直接畳み込み（リファレンス） */
static void DirectConvolve(
        const float *coef, uint32_t num_coef,
        const float *input, float *output, uint32_t num_samples)
{
    uint32_t i, j;

    memset(output, 0, sizeof(float) * num_samples);
    for (i = 0; i < num_samples; i++) {
        for (j = 0; j < num_coef; j++) {
            if (i + j < num_samples) {
                output[i + j] += coef[j] * input[i];
            }
        }
    }
}

static void ConvolveCheckSub(
        const struct RIConvolveInterface *convif, 
        const struct RIConvolveConfig *config,
        const float *input, uint32_t num_samples,
        const float *coef, uint32_t num_coefs)
{
    int32_t work_size;
    void *work, *conv;
    float *answer, *test;
    uint32_t smpl;

    work_size = convif->CalculateWorkSize(config);
    assert(work_size >= 0);
    work = malloc((size_t)work_size);
    conv = convif->Create(config, work, work_size);
    assert(conv != 0);

    answer = (float *)malloc(sizeof(float) * num_samples);
    test = (float *)malloc(sizeof(float) * num_samples);

    /* 正解作成 */
    DirectConvolve(coef, num_coefs, input, answer, num_samples);

    /* 係数セット */
    convif->SetCoefficients(conv, coef, num_coefs);

    /* 検証対象の畳み込み実行 */
    srand(0);
    smpl = 0;
    while (smpl < num_samples) {
        const uint32_t rand_input = (uint32_t)rand() % (config->max_num_input_samples + 1);
        const uint32_t num_block_samples = MIN(rand_input, num_samples - smpl);
        convif->Convolve(conv, &input[smpl], &test[smpl], num_block_samples);
        smpl += num_block_samples;
    }

    /* 一致確認 */
    {
        const uint32_t latency = (uint32_t)convif->GetLatencyNumSamples(conv);
        assert(latency < num_samples);
        for (smpl = 0; smpl < num_samples - latency; smpl++) {
            // printf("%d answer:%f actual:%f diff:%e \n", smpl, answer[smpl], test[smpl + latency], fabs(answer[smpl] - test[smpl + latency]));
            if (fabs(answer[smpl] - test[smpl + latency]) > FLOAT_EPSILON) {
                printf("test failed. %d answer:%f actual:%f diff:%e \n", smpl, answer[smpl], test[smpl + latency], fabs(answer[smpl] - test[smpl + latency]));
                FAIL();
            }
        }
    }

    convif->Destroy(conv);

    free(answer);
    free(test);
    free(work);
}

static void ConvolveCheck(
        const struct RIConvolveInterface *convif, 
        const struct RIConvolveConfig *config)
{
    const uint32_t num_input_samples = 8192;
    float *input, *coef;

    input = (float *)malloc(sizeof(float) * num_input_samples);
    coef = (float *)malloc(sizeof(float) * config->max_num_coefficients);

    /* 入力無音/係数無音 */
    {
        memset(input, 0, sizeof(float) * num_input_samples);
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
    }

    /* 入力インパルス/係数インパルス */
    {
        /* 先頭でインパルス */
        memset(input, 0, sizeof(float) * num_input_samples);
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        input[0] = coef[0] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* 入力を少しずらす */
        memset(input, 0, sizeof(float) * num_input_samples);
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        input[10] = 1.0f; coef[0] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルスを少しずらす */
        memset(input, 0, sizeof(float) * num_input_samples);
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        input[0] = 1.0f; coef[10] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルスを末尾に */
        memset(input, 0, sizeof(float) * num_input_samples);
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        input[0] = 1.0f; coef[config->max_num_coefficients - 1] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
    }

    /* 入力サイン波 */
    {
        uint32_t smpl;

        for (smpl = 0; smpl < num_input_samples; smpl++) {
            input[smpl] = (float)sin(2.0f * M_PI * 440.0f * smpl / 44100.0f);
        }

        /* 先頭でインパルス */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[0] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
        /* インパルスを少しずらす */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[10] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルスを末尾に */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[config->max_num_coefficients - 1] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルス全部定数 */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        for (smpl = 0; smpl < config->max_num_coefficients; smpl++) {
            coef[smpl] = 1.0f / config->max_num_coefficients;
        }
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* 雑音畳み込み */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        srand(100);
        for (smpl = 0; smpl < config->max_num_coefficients; smpl++) {
            coef[smpl] = 2.0f * ((float) rand() / RAND_MAX - 0.5f);
        }
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
    }

    /* 入力単調増加信号 */
    {
        uint32_t smpl;

        for (smpl = 0; smpl < num_input_samples; smpl++) {
            input[smpl] = (float)smpl / num_input_samples;
        }

        /* 先頭でインパルス */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[0] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
        /* インパルスを少しずらす */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[10] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルスを末尾に */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[config->max_num_coefficients - 1] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルス全部定数 */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        for (smpl = 0; smpl < config->max_num_coefficients; smpl++) {
            coef[smpl] = 1.0f / config->max_num_coefficients;
        }
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* 雑音畳み込み */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        srand(100);
        for (smpl = 0; smpl < config->max_num_coefficients; smpl++) {
            coef[smpl] = 2.0f * ((float) rand() / RAND_MAX - 0.5f);
        }
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
    }

    /* 白色雑音 */
    {
        uint32_t smpl;

        srand(0);
        for (smpl = 0; smpl < num_input_samples; smpl++) {
            input[smpl] = 2.0f * ((float) rand() / RAND_MAX - 0.5f);
        }

        /* 先頭でインパルス */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[0] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
        /* インパルスを少しずらす */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[10] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルスを末尾に */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        coef[config->max_num_coefficients - 1] = 1.0f;
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);

        /* インパルス全部定数 */
        memset(coef, 0, sizeof(float) * config->max_num_coefficients);
        for (smpl = 0; smpl < config->max_num_coefficients; smpl++) {
            coef[smpl] = 1.0f / config->max_num_coefficients;
        }
        ConvolveCheckSub(convif, config, input, num_input_samples, coef, config->max_num_coefficients);
    }

    free(coef);
    free(input);
}

/* 畳み込み一致確認テスト */
TEST(RIConvolveTest, ConvolveTest)
{
    struct RIConvolveConfig config;

    config.max_num_coefficients = 200;
    config.max_num_input_samples = 256;
    ConvolveCheck(RIKaratsuba_GetInterface(), &config);
    ConvolveCheck(RIFFTConvolve_GetInterface(), &config);
    ConvolveCheck(RIbaraConvolve_GetInterface(), &config);

    config.max_num_coefficients = 10000;
    config.max_num_input_samples = 512;
    ConvolveCheck(RIFFTConvolve_GetInterface(), &config);
    ConvolveCheck(RIbaraConvolve_GetInterface(), &config);
}



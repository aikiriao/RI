#ifndef RIRINGBUFFER_H_INCLUDED
#define RIRINGBUFFER_H_INCLUDED

#include <stdint.h>
#include <stddef.h>
#include <limits.h>

#if CHAR_BIT != 8
#error "This program run at must be CHAR_BIT == 8"
#endif

/* リングバッファ生成コンフィグ */
struct RIRingBufferConfig {
    size_t max_size; /* バッファサイズ */
    size_t max_required_size; /* 取り出し最大サイズ */
};

typedef enum RIRingBufferApiResult {
    RIRINGBUFFER_APIRESULT_OK = 0,
    RIRINGBUFFER_APIRESULT_INVALID_ARGUMENT,
    RIRINGBUFFER_APIRESULT_EXCEED_MAX_CAPACITY,
    RIRINGBUFFER_APIRESULT_EXCEED_MAX_REMAIN,
    RIRINGBUFFER_APIRESULT_EXCEED_MAX_REQUIRED,
    RIRINGBUFFER_APIRESULT_NG
} RIRingBufferApiResult;

struct RIRingBuffer;

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* リングバッファ作成に必要なワークサイズ計算 */
int32_t RIRingBuffer_CalculateWorkSize(const struct RIRingBufferConfig *config);

/* リングバッファ作成 */
struct RIRingBuffer *RIRingBuffer_Create(const struct RIRingBufferConfig *config, void *work, int32_t work_size);

/* リングバッファ破棄 */
void RIRingBuffer_Destroy(struct RIRingBuffer *buffer);

/* リングバッファの内容をクリア */
void RIRingBuffer_Clear(struct RIRingBuffer *buffer);

/* リングバッファ内に残ったデータサイズ取得 */
size_t RIRingBuffer_GetRemainSize(const struct RIRingBuffer *buffer);

/* リングバッファ内の空き領域サイズ取得 */
size_t RIRingBuffer_GetCapacitySize(const struct RIRingBuffer *buffer);

/* データ挿入 */
RIRingBufferApiResult RIRingBuffer_Put(
        struct RIRingBuffer *buffer, const void *data, size_t size);

/* データ見るだけ（バッファの状態は更新されない） 注意）バッファが一周する前に使用しないと上書きされる */
RIRingBufferApiResult RIRingBuffer_Peek(
        struct RIRingBuffer *buffer, void **pdata, size_t required_size);

/* データ取得 注意）バッファが一周する前に使用しないと上書きされる */
RIRingBufferApiResult RIRingBuffer_Get(
        struct RIRingBuffer *buffer, void **pdata, size_t required_size);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* RIRINGBUFFER_H_INCLUDED */

#include "ri_ring_buffer.h"

#include <stdio.h>
#include <string.h>
#include <assert.h>

/* メモリアラインメント */
#define RIRINGBUFFER_ALIGNMENT 16
/* nの倍数への切り上げ */
#define RIRINGBUFFER_ROUNDUP(val, n) ((((val) + ((n) - 1)) / (n)) * (n))
/* 最小値の取得 */
#define RIRINGBUFFER_MIN(a,b) (((a) < (b)) ? (a) : (b))

/* リングバッファ */
struct RIRingBuffer {
    uint8_t *data; /* データ領域の先頭ポインタ データは8ビットデータ列と考える */
    size_t buffer_size; /* バッファデータサイズ */
    size_t max_required_size; /* 最大要求データサイズ */
    uint32_t read_pos; /* 読み出し位置 */
    uint32_t write_pos; /* 書き出し位置 */
};

/* リングバッファ作成に必要なワークサイズ計算 */
int32_t RIRingBuffer_CalculateWorkSize(const struct RIRingBufferConfig *config)
{
    int32_t work_size;

    /* 引数チェック */
    if (config == NULL) {
        return -1;
    }
    
    /* バッファサイズは要求サイズより大きい */
    if (config->max_size < config->max_required_size) {
        return -1;
    }

    work_size = sizeof(struct RIRingBuffer) + RIRINGBUFFER_ALIGNMENT;
    work_size += config->max_size + 1 + RIRINGBUFFER_ALIGNMENT;
    work_size += config->max_required_size;

    return work_size;
}

/* リングバッファ作成 */
struct RIRingBuffer *RIRingBuffer_Create(const struct RIRingBufferConfig *config, void *work, int32_t work_size)
{
    struct RIRingBuffer *buffer;
    uint8_t *work_ptr;

    /* 引数チェック */
    if ((config == NULL) || (work == NULL) || (work_size < 0)) {
        return NULL;
    }

    if (work_size < RIRingBuffer_CalculateWorkSize(config)) {
        return NULL;
    }

    /* ハンドル領域割当 */
    work_ptr = (uint8_t *)RIRINGBUFFER_ROUNDUP((uintptr_t)work, RIRINGBUFFER_ALIGNMENT);
    buffer = (struct RIRingBuffer *)work_ptr;
    work_ptr += sizeof(struct RIRingBuffer);

    /* サイズを記録 */
    buffer->buffer_size = config->max_size + 1; /* バッファの位置関係を正しく解釈するため1要素分多く確保する（write_pos == read_pos のときデータが一杯なのか空なのか判定できない） */
    buffer->max_required_size = config->max_required_size;

    /* バッファ領域割当 */
    work_ptr = (uint8_t *)RIRINGBUFFER_ROUNDUP((uintptr_t)work_ptr, RIRINGBUFFER_ALIGNMENT);
    buffer->data = work_ptr;
    work_ptr += (buffer->buffer_size + config->max_required_size);

    /* バッファの内容をクリア */
    RIRingBuffer_Clear(buffer);

    return buffer;
}

/* リングバッファ破棄 */
void RIRingBuffer_Destroy(struct RIRingBuffer *buffer)
{
    /* 不定領域アクセス防止のため内容はクリア */
    RIRingBuffer_Clear(buffer);
}

/* リングバッファの内容をクリア */
void RIRingBuffer_Clear(struct RIRingBuffer *buffer)
{
    assert(buffer != NULL);

    /* データ領域を0埋め */
    memset(buffer->data, 0, buffer->buffer_size + buffer->max_required_size);

    /* バッファ参照位置を初期化 */
    buffer->read_pos = 0;
    buffer->write_pos = 0;
}

/* リングバッファ内に残ったデータサイズ取得 */
size_t RIRingBuffer_GetRemainSize(const struct RIRingBuffer *buffer)
{
    assert(buffer != NULL);

    if (buffer->read_pos > buffer->write_pos) {
        return buffer->buffer_size + buffer->write_pos - buffer->read_pos;
    }

    return buffer->write_pos - buffer->read_pos;
}

/* リングバッファ内の空き領域サイズ取得 */
size_t RIRingBuffer_GetCapacitySize(const struct RIRingBuffer *buffer)
{
    assert(buffer != NULL);
    assert(buffer->buffer_size >= RIRingBuffer_GetRemainSize(buffer));

    /* 実際に入るサイズはバッファサイズより1バイト少ない */
    return buffer->buffer_size - RIRingBuffer_GetRemainSize(buffer) - 1;
}

/* データ挿入 */
RIRingBufferApiResult RIRingBuffer_Put(
        struct RIRingBuffer *buffer, const void *data, size_t size)
{
    /* 引数チェック */
    if ((buffer == NULL) || (data == NULL) || (size == 0)) {
        return RIRINGBUFFER_APIRESULT_INVALID_ARGUMENT;
    }

    /* バッファに空き領域がない */
    if (size > RIRingBuffer_GetCapacitySize(buffer)) {
        return RIRINGBUFFER_APIRESULT_EXCEED_MAX_CAPACITY;
    }

    /* リングバッファを巡回するケース: バッファ末尾までまず書き込み */
    if (buffer->write_pos + size >= buffer->buffer_size) {
        uint8_t *wp = buffer->data + buffer->write_pos;
        const size_t data_head_size = buffer->buffer_size - buffer->write_pos;
        memcpy(wp, data, data_head_size);
        data = (const void *)((uint8_t *)data + data_head_size);
        size -= data_head_size;
        buffer->write_pos = 0;
        if (size == 0) {
            return RIRINGBUFFER_APIRESULT_OK;
        }
    }

    /* 剰余領域への書き込み */
    if (buffer->write_pos < buffer->max_required_size) {
        uint8_t *wp = buffer->data + buffer->buffer_size + buffer->write_pos;
        const size_t copy_size = RIRINGBUFFER_MIN(size, buffer->max_required_size - buffer->write_pos);
        memcpy(wp, data, copy_size);
    }

    /* リングバッファへの書き込み */
    memcpy(buffer->data + buffer->write_pos, data, size);
    buffer->write_pos += size; /* 巡回するケースでインデックスの剰余処理済 */

    return RIRINGBUFFER_APIRESULT_OK;
}

/* データ見るだけ（バッファの状態は更新されない） 注意）バッファが一周する前に使用しないと上書きされる */
RIRingBufferApiResult RIRingBuffer_Peek(
        struct RIRingBuffer *buffer, void **pdata, size_t required_size)
{
    /* 引数チェック */
    if ((buffer == NULL) || (pdata == NULL) || (required_size == 0)) {
        return RIRINGBUFFER_APIRESULT_INVALID_ARGUMENT;
    }

    /* 最大要求サイズを超えている */
    if (required_size > buffer->max_required_size) {
        return RIRINGBUFFER_APIRESULT_EXCEED_MAX_REQUIRED;
    }

    /* 残りデータサイズを超えている */
    if (required_size > RIRingBuffer_GetRemainSize(buffer)) {
        return RIRINGBUFFER_APIRESULT_EXCEED_MAX_REMAIN;
    }

    /* データの参照取得 */
    (*pdata) = (void *)(buffer->data + buffer->read_pos);

    return RIRINGBUFFER_APIRESULT_OK;
}

/* データ取得 注意）バッファが一周する前に使用しないと上書きされる */
RIRingBufferApiResult RIRingBuffer_Get(
        struct RIRingBuffer *buffer, void **pdata, size_t required_size)
{
    RIRingBufferApiResult ret;

    /* 読み出し */
    if ((ret = RIRingBuffer_Peek(buffer, pdata, required_size)) != RIRINGBUFFER_APIRESULT_OK) {
        return ret;
    }

    /* バッファ参照位置更新 */
    buffer->read_pos = (buffer->read_pos + (uint32_t)required_size) % buffer->buffer_size;

    return RIRINGBUFFER_APIRESULT_OK;
}


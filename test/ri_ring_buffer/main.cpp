#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/ri_ring_buffer/src/ri_ring_buffer.c"
}

/* Put / Getテスト */
TEST(RIRingBufferTest, PutGetTest)
{
    {
        int32_t work_size;
        void *work;
        struct RIRingBuffer *buf;
        struct RIRingBufferConfig config;
        const char data[] = "0123456789";
        char *tmp;

        config.max_size = 6;
        config.max_required_size = 3;

        work_size = RIRingBuffer_CalculateWorkSize(&config);
        ASSERT_TRUE(work_size >= 0);
        work = malloc(work_size);

        buf = RIRingBuffer_Create(&config, work, work_size);
        ASSERT_TRUE(buf != NULL);

        EXPECT_EQ(0, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(6, RIRingBuffer_GetCapacitySize(buf));

        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Put(buf, data, 1));
        EXPECT_EQ(1, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(5, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 1));
        EXPECT_EQ(tmp[0], data[0]);
        EXPECT_EQ(0, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(6, RIRingBuffer_GetCapacitySize(buf));

        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Put(buf, data, 6));
        EXPECT_EQ(6, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(0, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 3));
        EXPECT_EQ(0, memcmp(tmp, &data[0], 3));
        EXPECT_EQ(3, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(3, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 3));
        EXPECT_EQ(0, memcmp(tmp, &data[3], 3));
        EXPECT_EQ(0, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(6, RIRingBuffer_GetCapacitySize(buf));

        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Put(buf, &data[0], 2));
        EXPECT_EQ(2, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(4, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Put(buf, &data[2], 2));
        EXPECT_EQ(4, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(2, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 3));
        EXPECT_EQ(0, memcmp(tmp, &data[0], 3));
        EXPECT_EQ(1, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(5, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Put(buf, &data[4], 2));
        EXPECT_EQ(3, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(3, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 3));
        EXPECT_EQ(0, memcmp(tmp, &data[3], 3));
        EXPECT_EQ(0, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(6, RIRingBuffer_GetCapacitySize(buf));

        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Put(buf, &data[0], 5));
        EXPECT_EQ(5, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(1, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 3));
        EXPECT_EQ(0, memcmp(tmp, &data[0], 3));
        EXPECT_EQ(2, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(4, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 2));
        EXPECT_EQ(0, memcmp(tmp, &data[3], 2));
        EXPECT_EQ(0, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(6, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Put(buf, &data[0], 5));
        EXPECT_EQ(5, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(1, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 3));
        EXPECT_EQ(0, memcmp(tmp, &data[0], 3));
        EXPECT_EQ(2, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(4, RIRingBuffer_GetCapacitySize(buf));
        EXPECT_EQ(RIRINGBUFFER_APIRESULT_OK, RIRingBuffer_Get(buf, (void **)&tmp, 2));
        EXPECT_EQ(0, memcmp(tmp, &data[3], 2));
        EXPECT_EQ(0, RIRingBuffer_GetRemainSize(buf));
        EXPECT_EQ(6, RIRingBuffer_GetCapacitySize(buf));

        RIRingBuffer_Destroy(buf);
        free(buf);
    }
}

int main(int argc, char **argv)
{
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}

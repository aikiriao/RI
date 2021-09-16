#include <stdlib.h>
#include <string.h>

#include <gtest/gtest.h>

/* テスト対象のモジュール */
extern "C" {
#include "../../libs/ri_convolve/src/ri_zerolatency_fft_convolve.c"
}

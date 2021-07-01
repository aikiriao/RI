#ifndef RIKARATSUBA_H_INCLUDED
#define RIKARATSUBA_H_INCLUDED

#include "ri_convolve.h"

#ifdef __cplusplus
extern "C" {
#endif

/* インターフェース取得 */
const struct RIConvolveInterface* RIKaratsuba_GetInterface(void);

#ifdef __cplusplus
}
#endif

#endif /* RIKARATSUBA_H_INCLUDED */

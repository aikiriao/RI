#ifndef RIFFT_H_INCLUDED
#define RIFFT_H_INCLUDED

/* 複素数アクセスマクロ */
#define RIFFTCOMPLEX_REAL(flt_array, i) ((flt_array)[((i) << 1)])
#define RIFFTCOMPLEX_IMAG(flt_array, i) ((flt_array)[((i) << 1) + 1])

#ifdef __cplusplus
extern "C" {
#endif

/* FFT 正規化は行いません
* n 系列長
* flag -1:FFT, 1:IFFT
* x フーリエ変換する系列(入出力 2nサイズ必須, 偶数番目に実数部, 奇数番目に虚数部)
* y 作業用配列(xと同一サイズ)
*/
void RIFFT_FloatFFT(int n, int flag, float *x, float *y);

/* 実数列のFFT 正規化は行いません 正規化定数は2/n
* n 系列長
* flag -1:FFT, 1:IFFT
* x フーリエ変換する系列(入出力 nサイズ必須, FFTの場合, x[0]に直流成分の実部, x[1]に最高周波数成分の虚数部が入る)
* y 作業用配列(xと同一サイズ)
*/
void RIFFT_RealFFT(int n, int flag, float *x, float *y);

#ifdef __cplusplus
}
#endif

#endif /* RIFFT_H_INCLUDED */

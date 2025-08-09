// SPDX-FileCopyrightText: None
// SPDX-License-Identifier: Unlicense

#ifndef _SIMPLE_FFT_H_
#define _SIMPLE_FFT_H_

// FFT configuration / service data struct
// this struct was intentionally made a part of the interface
// to allow compile-time initialization with static data
typedef struct {
  unsigned int n;     // FFTs count, should be power of 2
  const float* tw;    // twiddle factors, 2n (n *pairs*)
  float tw_mul_re;    // real and imaginary parts of the
  float tw_mul_im;    // special value used for real FFT
} simple_fft_cfg;

// initializes all the fields in configuration struct
// there is no extra initialization logic behind it, so
// it is not strictly necessary to call this function,
// configuration struct can be initialized manually with
// the static data previously returned by this function
// cfg - simple_fft_cfg struct to fill
// tw - buffer used for twiddle factors, 2*N values
// N - FFTs count, must be data buffer size / 2
// function doesn't do any dynamic memory allocation,
// pointer as argument directly used for field initialization
void fft_init(simple_fft_cfg* cfg, float* tw, unsigned int N);

// does in-place FFT transform
// output format is the same as in KISS FFT C++
// cfg - FFT configuration (see above)
// data is an array of (re,im) pairs, N in total
void fft_cplx(const simple_fft_cfg* cfg, float* data);

// does in-place FFT transform
// output format is the same as in KISS FFT C++
// cfg - FFT configuration (see above)
// data is an array of real values, 2*N in total
void fft_real(const simple_fft_cfg* cfg, float* data);

#endif  // _SIMPLE_FFT_H_

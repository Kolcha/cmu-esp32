// SPDX-FileCopyrightText: None
// SPDX-License-Identifier: Unlicense

#include "simple_fft.h"

#include <complex.h>
#include <math.h>

void fft_init(simple_fft_cfg* cfg, float* tw, unsigned int N)
{
  cfg->n = N;

  const float pi = acosf(-1.f);
  const float phinc = -2 * pi / N;
  for (unsigned int i = 0; i < N; i++) {
    tw[2*i+0] = cosf(i*phinc);
    tw[2*i+1] = sinf(i*phinc);
  }
  cfg->tw = tw;

  // special last item that used by real FFT
  const float half_phi_inc = -pi / N;
  cfg->tw_mul_re = cosf(half_phi_inc);
  cfg->tw_mul_im = sinf(half_phi_inc);
}

static void rearrange(float complex* data, const unsigned int N)
{
  unsigned int target = 0;
  for (unsigned int position = 0; position < N; position++) {
    if (target > position) {
      const float complex temp = data[target];
      data[target] = data[position];
      data[position] = temp;
    }

    unsigned int mask = N;
    while(target & (mask >>=1))
      target &= ~mask;
    target |= mask;
  }
}

// does FFT, data must be processed by rearrange() before calling this
// the code is based on https://github.com/lloydroc/arduino_fft
static void compute(float complex* data, const float* tw, const unsigned int N)
{
  for (unsigned int step = 1; step < N; step <<= 1) {
    const unsigned int jump = step << 1;
    float twiddle_re = 1.f;
    float twiddle_im = 0.f;
    for (unsigned int group = 0; group < step; group++) {
      for (unsigned int pair = group; pair < N; pair += jump) {
        const unsigned int match = pair + step;
        const float complex product =
          (twiddle_re*crealf(data[match])-twiddle_im*cimagf(data[match])) +
          (twiddle_im*crealf(data[match])+twiddle_re*cimagf(data[match]))*I;
        data[match] = data[pair] - product;
        data[pair] += product;
      }

      // we need the factors below for the next iteration
      // if we don't iterate, then don't compute
      if(group+1 == step)
        continue;

      const unsigned int tw_idx = N/2 * (group+1)/step;
      twiddle_re = tw[2*tw_idx+0];
      twiddle_im = tw[2*tw_idx+1];
    }
  }
}

void fft_cplx(const simple_fft_cfg* cfg, float* data)
{
  rearrange((float complex*)data, cfg->n);
  compute((float complex*)data, cfg->tw, cfg->n);
}

// the code is based on KISS FFT C++ implementation of real data transform
// https://github.com/mborgerding/kissfft/blob/master/kissfft.hh
static void postprocess(const simple_fft_cfg* cfg, float complex* dst)
{
  const unsigned int N = cfg->n;
  // post-processing for k = 0 and k = N
  dst[0] = (crealf(dst[0]) + cimagf(dst[0])) +
           (crealf(dst[0]) - cimagf(dst[0]))*I;

  // post-processing for all the other k = 1, 2, ..., N-1
  const float complex* twiddles = (float complex*)cfg->tw;
  const float complex twiddle_mul = cfg->tw_mul_re + cfg->tw_mul_im*I;
  for (unsigned int k = 1; 2*k < N; ++k ) {
    const float complex w = 0.5f * (
                              (crealf( dst[k]) + crealf(dst[N-k])) +
                              (cimagf( dst[k]) - cimagf(dst[N-k]))*I);
    const float complex z = 0.5f * (
                              (cimagf( dst[k]) + cimagf(dst[N-k])) +
                              (crealf(-dst[k]) + crealf(dst[N-k]))*I);
    const float complex twiddle = k % 2 == 0 ?
                                  twiddles[k/2] :
                                  twiddles[k/2] * twiddle_mul;
    dst[  k] =       w + twiddle * z;
    dst[N-k] = conjf(w - twiddle * z);
  }
  if (N % 2 == 0)
    dst[N/2] = conjf(dst[N/2]);
}

void fft_real(const simple_fft_cfg* cfg, float* data)
{
  fft_cplx(cfg, data);
  postprocess(cfg, (float complex*)data);
}

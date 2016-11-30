/*-
 * Copyright 2014 phm
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "config.h"
#include "miner.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "sph/sph_blake.h"
#include "sph/sph_groestl.h"
#include "sph/sph_jh.h"
#include "sph/sph_keccak.h"
#include "sph/sph_skein.h"

/* Move init out of loop, so init once externally, and then use one single
 * memcpy with that bigger memory block */
typedef struct {
  sph_blake512_context blake1;
  sph_groestl512_context groestl1;
  sph_jh512_context jh1;
  sph_keccak512_context keccak1;
  sph_skein512_context skein1;
} Xhash_context_holder;

static Xhash_context_holder base_contexts;

void init_Nhash_contexts() {
  sph_blake512_init(&base_contexts.blake1);
  sph_groestl512_init(&base_contexts.groestl1);
  sph_jh512_init(&base_contexts.jh1);
  sph_keccak512_init(&base_contexts.keccak1);
  sph_skein512_init(&base_contexts.skein1);
}

#ifdef __APPLE_CC__
static
#endif
    void
    talkhash(void *state, const void *input) {
  init_Nhash_contexts();

  Xhash_context_holder ctx;

  uint32_t hashA[16], hashB[16];
  // blake-bmw-groestl-sken-jh-meccak-luffa-cubehash-shivite-simd-echo
  memcpy(&ctx, &base_contexts, sizeof(base_contexts));

  sph_blake512(&ctx.blake1, input, 80);
  sph_blake512_close(&ctx.blake1, hashA);

  sph_groestl512(&ctx.groestl1, hashA, 64);
  sph_groestl512_close(&ctx.groestl1, hashB);

  sph_jh512(&ctx.jh1, hashB, 64);
  sph_jh512_close(&ctx.jh1, hashA);

  sph_keccak512(&ctx.keccak1, hashA, 64);
  sph_keccak512_close(&ctx.keccak1, hashB);

  sph_skein512(&ctx.skein1, hashB, 64);
  sph_skein512_close(&ctx.skein1, hashA);

  memcpy(state, hashA, 32);
}

static const uint32_t diff1targ = 0x0000ffff;

/* Used externally as confirmation of correct OCL code */
int talkcoin_test(unsigned char *pdata, const unsigned char *ptarget,
                  uint32_t nonce) {
  uint32_t tmp_hash7, Htarg = le32toh(((const uint32_t *)ptarget)[7]);
  uint32_t data[20], ohash[8];

  be32enc_vect(data, (const uint32_t *)pdata, 19);
  data[19] = htobe32(nonce);
  talkhash(ohash, data);

  tmp_hash7 = be32toh(ohash[7]);

  applog(LOG_DEBUG, "htarget %08lx diff1 %08lx hash %08lx",
         (long unsigned int)Htarg, (long unsigned int)diff1targ,
         (long unsigned int)tmp_hash7);

  if (tmp_hash7 > diff1targ)
    return -1;

  if (tmp_hash7 > Htarg)
    return 0;

  return 1;
}

void talkcoin_regenhash(struct work *work) {
  uint32_t data[20];
  uint32_t *nonce = (uint32_t *)(work->data + 76);
  uint32_t *ohash = (uint32_t *)(work->hash);

  be32enc_vect(data, (const uint32_t *)work->data, 19);
  data[19] = htobe32(*nonce);
  talkhash(ohash, data);
}

bool scanhash_talkcoin(struct thr_info *thr,
                       const unsigned char __maybe_unused *pmidstate,
                       unsigned char *pdata,
                       unsigned char __maybe_unused *phash1,
                       unsigned char __maybe_unused *phash,
                       const unsigned char *ptarget, uint32_t max_nonce,
                       uint32_t *last_nonce, uint32_t n) {
  uint32_t *nonce = (uint32_t *)(pdata + 76);
  uint32_t data[20];
  uint32_t tmp_hash7;
  uint32_t Htarg = le32toh(((const uint32_t *)ptarget)[7]);
  bool ret = false;

  be32enc_vect(data, (const uint32_t *)pdata, 19);

  while (1) {
    uint32_t ostate[8];

    *nonce = ++n;
    data[19] = (n);
    talkhash(ostate, data);
    tmp_hash7 = (ostate[7]);

    applog(LOG_INFO, "data7 %08lx", (long unsigned int)data[7]);

    if (unlikely(tmp_hash7 <= Htarg)) {
      ((uint32_t *)pdata)[19] = htobe32(n);
      *last_nonce = n;
      ret = true;
      break;
    }

    if (unlikely((n >= max_nonce) || thr->work_restart)) {
      *last_nonce = n;
      break;
    }
  }

  return ret;
}

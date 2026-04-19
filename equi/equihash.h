#ifndef EQUIHASH_H
#define EQUIHASH_H

#include <stdint.h>

// miner nonce "cursor" unique for each thread
#define EQNONCE_OFFSET 30 /* 27:34 */

#define WK 9
#define WN 200

/* ZelHash (Flux) = Equihash 125,4 */
#define ZELWN          125
#define ZELWK          4
#define ZEL_PROOFSIZE  (1 << ZELWK)   /* 16 indices per solution */
/* Compressed solution size for Equihash(125,4):
 *   PROOFSIZE * (N/(K+1) + 1) / 8 = 16 * 26 / 8 = 52 bytes */
#define ZEL_SOLSIZE    52
/* Block header (140 B) + size varint (3 B) + solution (52 B) */
#define ZEL_FULLSIZE   (140 + 3 + ZEL_SOLSIZE)
//#define CONFIG_MODE_1 9, 1248, 12, 640, packer_cantor /* eqcuda.hpp */

extern "C" {
	void equi_hash(const void* input, void* output, int len);
	int  equi_verify_sol(void* const hdr, void* const soln);
	bool equi_verify(uint8_t* const hdr, uint8_t* const soln);
	/* ZelHash (Flux) variant */
	void zel_hash(const void* input, void* output, int len);
	int  zel_verify_sol(void* const hdr, void* const soln);
	bool zel_verify(uint8_t* const hdr, uint8_t* const soln);
}

#endif

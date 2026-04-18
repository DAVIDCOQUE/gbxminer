#include "miner.h"

int scanhash_c11(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_cryptolight(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done, int variant) { return 0; }
int scanhash_cryptonight(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done, int variant) { return 0; }
int scanhash_decred(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_fresh(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_hmq17(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_hsr(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_luffa(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_pentablake(int thr_id, struct work *work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_qubit(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_sib(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_s3(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_sonoa(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_timetravel(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_exosis(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_vanilla(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done, int8_t blake_rounds) { return 0; }
int scanhash_veltor(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_whirl(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_wildkeccak(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_x11evo(int thr_id, struct work* work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }
int scanhash_zr5(int thr_id, struct work *work, uint32_t max_nonce, unsigned long *hashes_done) { return 0; }

void free_bitcore(int thr_id) { }
void free_c11(int thr_id) { }
void free_cryptolight(int thr_id) { }
void free_cryptonight(int thr_id) { }
void free_decred(int thr_id) { }
void free_exosis(int thr_id) { }
void free_fresh(int thr_id) { }
void free_hmq17(int thr_id) { }
void free_hsr(int thr_id) { }
void free_luffa(int thr_id) { }
void free_pentablake(int thr_id) { }
void free_qubit(int thr_id) { }
void free_sib(int thr_id) { }
void free_s3(int thr_id) { }
void free_timetravel(int thr_id) { }
void free_vanilla(int thr_id) { }
void free_veltor(int thr_id) { }
void free_whirl(int thr_id) { }
void free_wildkeccak(int thr_id) { }
void free_x11evo(int thr_id) { }
void free_zr5(int thr_id) { }
void free_sonoa(int thr_id) { }

// X11-X17 shared CPU function stubs (used by bastion, skunk, polytimos, qubit, deep, tribus, phi)
void x11_luffa512_cpu_init(int thr_id, unsigned int threads) { }
void x11_luffa512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x11_luffa512_cpu_free(int thr_id) { }

void x11_echo512_cpu_init(int thr_id, unsigned int threads) { }
void x11_echo512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x11_echo512_cpu_free(int thr_id) { }

void x11_cubehash512_cpu_init(int thr_id, unsigned int threads) { }
void x11_cubehash512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x11_cubehash512_cpu_free(int thr_id) { }

void x11_shavite512_cpu_init(int thr_id, unsigned int threads) { }
void x11_shavite512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x11_shavite512_cpu_free(int thr_id) { }

void x11_simd512_cpu_init(int thr_id, unsigned int threads) { }
void x11_simd512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x11_simd512_cpu_free(int thr_id) { }

void x13_hamsi512_cpu_init(int thr_id, unsigned int threads) { }
void x13_hamsi512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x13_hamsi512_cpu_free(int thr_id) { }

void x13_fugue512_cpu_init(int thr_id, unsigned int threads) { }
void x13_fugue512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x13_fugue512_cpu_free(int thr_id) { }

void x14_shabal512_cpu_init(int thr_id, unsigned int threads) { }
void x14_shabal512_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x14_shabal512_cpu_free(int thr_id) { }

void x15_whirlpool_cpu_init(int thr_id, unsigned int threads, int cups) { }
void x15_whirlpool_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* resNonce, unsigned int* hashOutput, int intensity) { }
void x15_whirlpool_cpu_free(int thr_id) { }

// Streebog/SM3 function stubs
void streebog_cpu_hash_64(int thr_id, unsigned int startNonce, unsigned int* hashOutput) { }
void streebog_hash_64_maxwell(int thr_id, unsigned int startNonce, unsigned int* hashOutput) { }
void streebog_sm3_set_target(unsigned int* target) { }
void streebog_sm3_hash_64_final(int thr_id, unsigned int startNonce, unsigned int* resNonce, unsigned int* hashOutput) { }

// Phi2 filtered hash stubs
void phi_streebog_hash_64_filtered(int thr_id, unsigned int startNonce, unsigned int* resNonce, unsigned int* hashOutput) { }
void phi_echo512_cpu_hash_64_filtered(int thr_id, unsigned int startNonce, unsigned int* resNonce, unsigned int* hashOutput) { }

// Phi2 cubehash stubs
void cubehash512_cuda_hash_80(int thr_id, unsigned int startNonce, unsigned int numHashes, unsigned int* hashOutput) { }
void cubehash512_setBlock_80(int thr_id, unsigned int* data) { }

void bastionhash(void* output, const void* input) { }
void bitcore_hash(void* output, const void* input) { }
void c11hash(void* output, const void* input) { }
void cryptolight_hash(void* output, const void* input) { }
void cryptonight_hash(void* output, const void* input) { }
void decred_hash(void* output, const void* input) { }
void deephash(void* output, const void* input) { }
void exosis_hash(void* output, const void* input) { }
void fresh_hash(void* output, const void* input) { }
void heavyhash(void* output, const void* input) { }
void hmq17hash(void* output, const void* input) { }
void hsr_hash(void* output, const void* input) { }
void jackpot_hash(void* output, const void* input) { }
void luffahash(void* output, const void* input) { }
void lyra2_hash(void* output, const void* input) { }
void lyra2z_hash(void* output, const void* input) { }
void myr_gr_hash(void* output, const void* input) { }
void pentablakehash(void* output, const void* input) { }
void qubithash(void* output, const void* input) { }
void sibhash(void* output, const void* input) { }
void skunk_hash(void* output, const void* input) { }
void s3hash(void* output, const void* input) { }
void timetravel_hash(void* output, const void* input) { }
void vanilla_hash(void* output, const void* input) { }
void veltorhash(void* output, const void* input) { }
void wcoinhash(void* output, const void* input) { }
void wildkeccak_hash(void* output, const void* input) { }
void x11evo_hash(void* output, const void* input) { }
void zr5hash(void* output, const void* input) { }
void sonoa_hash(void* output, const void* input) { }

bool opt_scratchpad_url = false;

bool rpc2_stratum_authorize(struct stratum_ctx *sctx, const char *user, const char *pass) { return false; }
bool rpc2_stratum_job(struct stratum_ctx *sctx, json_t *id, json_t *params) { return false; }
bool rpc2_stratum_thread_stuff(struct pool_infos *pool) { return false; }

unsigned long scratchpad_size = 0;
void GetScratchpad() { }

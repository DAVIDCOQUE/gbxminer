/**
 * Made to benchmark and test algo switch
 *
 * 2015 - tpruvot@github
 */

#include <unistd.h>
#include <math.h>

#include "miner.h"
#include "algos.h"
#include "nvml.h"
#include <cuda_runtime.h>

#ifdef __APPLE__
#include "compat/pthreads/pthread_barrier.hpp"
#endif

extern char driver_version[32];

int bench_algo = -1;

static double algo_hashrates[MAX_GPUS][ALGO_COUNT] = { 0 };
static uint32_t algo_throughput[MAX_GPUS][ALGO_COUNT] = { 0 };
static int algo_mem_used[MAX_GPUS][ALGO_COUNT] = { 0 };
static int device_mem_free[MAX_GPUS] = { 0 };
static double algo_temps[MAX_GPUS][ALGO_COUNT] = { 0 };
static double algo_power[MAX_GPUS][ALGO_COUNT] = { 0 };

static pthread_barrier_t miner_barr;
static pthread_barrier_t algo_barr;
static pthread_mutex_t bench_lock = PTHREAD_MUTEX_INITIALIZER;

extern double thr_hashrates[MAX_GPUS];

void bench_init(int threads)
{
	bench_algo = opt_algo = (enum sha_algos) 0; /* first */
	applog(LOG_BLUE, "Starting benchmark mode with %s", algo_names[opt_algo]);
	pthread_barrier_init(&miner_barr, NULL, threads);
	pthread_barrier_init(&algo_barr, NULL, threads);
	// required for usage of first algo.
	for (int n=0; n < opt_n_threads; n++) {
		device_mem_free[n] = cuda_available_memory(n);
	}
}

void bench_free()
{
	pthread_barrier_destroy(&miner_barr);
	pthread_barrier_destroy(&algo_barr);
}

// required to switch algos
void algo_free_all(int thr_id)
{
	// only initialized algos will be freed
	free_allium(thr_id);
	free_bmw(thr_id);
	free_equihash(thr_id);
	free_etchash(thr_id);
	free_keccak256(thr_id);
	free_fugue256(thr_id);
	free_groestlcoin(thr_id);
#ifdef WITH_HEAVY_ALGO
	free_heavy(thr_id);
#endif
	free_jackpot(thr_id);
	free_jha(thr_id);
	free_kapow(thr_id);
	free_lbry(thr_id);
	free_luffa(thr_id);
	free_lyra2(thr_id);
	free_lyra2v2(thr_id);
	free_lyra2v3(thr_id);
	free_lyra2Z(thr_id);
	free_myriad(thr_id);
	free_neoscrypt(thr_id);
	free_nist5(thr_id);
	free_quark(thr_id);
	free_qubit(thr_id);
	free_skeincoin(thr_id);
	free_skein2(thr_id);
	free_sha256d(thr_id);
	free_sha256t(thr_id);
	free_whirl(thr_id);
}

// benchmark all algos (called once per mining thread)
bool bench_algo_switch_next(int thr_id)
{
	int algo = (int) opt_algo;
	int prev_algo = algo;
	int dev_id = device_map[thr_id % MAX_GPUS];
	int mfree, mused;
	// doesnt seems enough to prevent device slow down
	// after some algo switchs
	bool need_reset = (gpu_threads == 1);

	algo++;

	// skip duplicated/alias algos
	if (algo == ALGO_DMD_GR)    algo++; /* same as groestl */
	if (algo == ALGO_HEAVY)     algo++; /* dead */
	if (algo == ALGO_MJOLLNIR)  algo++; /* same as heavy */
	if (algo == ALGO_KECCAKC)   algo++; /* same as keccak */
	if (algo == ALGO_WHIRLCOIN) algo++; /* same as whirlpool */
	if (algo == ALGO_QUARK)     algo++; /* to fix */
	if (algo == ALGO_LBRY && CUDART_VERSION < 7000) algo++;

	if (device_sm[dev_id] && device_sm[dev_id] < 300) {
		// incompatible SM 2.1 kernels...
		if (algo == ALGO_GROESTL)   algo++;
		if (algo == ALGO_MYR_GR)    algo++;
		if (algo == ALGO_NEOSCRYPT) algo++;
	}

	// free current algo memory and track mem usage
	mused = cuda_available_memory(thr_id);
	algo_free_all(thr_id);
	CUDA_LOG_ERROR();

	// device can take some time to free
	mfree = cuda_available_memory(thr_id);
	if (device_mem_free[thr_id] > mfree) {
		sleep(1);
		mfree = cuda_available_memory(thr_id);
	}

	// we need to wait completion on all cards before the switch
	if (opt_n_threads > 1) {
		pthread_barrier_wait(&miner_barr);
	}

	char rate[32] = { 0 };
	double hashrate = stats_get_speed(thr_id, thr_hashrates[thr_id]);
	format_hashrate(hashrate, rate);
	gpulog(LOG_NOTICE, thr_id, "%s hashrate = %s", algo_names[prev_algo], rate);

	// ensure memory leak is still real after the barrier
	if (device_mem_free[thr_id] > mfree) {
		mfree = cuda_available_memory(thr_id);
	}

	// check if there is memory leak
	if (device_mem_free[thr_id] - mfree > 1) {
		gpulog(LOG_WARNING, thr_id, "possible %d MB memory leak in %s! %d MB free",
			(device_mem_free[thr_id] - mfree), algo_names[prev_algo], mfree);
		cuda_reset_device(thr_id, NULL); // force to free the leak
		need_reset = false;
		mfree = cuda_available_memory(thr_id);
	}
	// store used memory per algo
	algo_mem_used[thr_id][opt_algo] = device_mem_free[thr_id] - mused;
	device_mem_free[thr_id] = mfree;

	// store to dump a table per gpu later
	algo_hashrates[thr_id][prev_algo] = hashrate;

	// capture temperature and power for efficiency calculation
	struct cgpu_info *cgpu = &thr_info[thr_id].gpu;
	float temp = gpu_temp(cgpu);
	unsigned int power_mw = gpu_power(cgpu);
	algo_temps[thr_id][prev_algo] = temp;
	algo_power[thr_id][prev_algo] = power_mw / 1000.0; // convert to watts

	// wait the other threads to display logs correctly
	if (opt_n_threads > 1) {
		pthread_barrier_wait(&algo_barr);
	}

	if (algo == ALGO_AUTO)
		return false; // all algos done

	// mutex primary used for the stats purge
	pthread_mutex_lock(&bench_lock);
	stats_purge_all();

	opt_algo = (enum sha_algos) algo;
	global_hashrate = 0;
	thr_hashrates[thr_id] = 0; // reset for minmax64
	pthread_mutex_unlock(&bench_lock);

	if (need_reset)
		cuda_reset_device(thr_id, NULL);

	if (thr_id == 0)
		applog(LOG_BLUE, "Benchmark algo %s...", algo_names[algo]);

	return true;
}

void bench_set_throughput(int thr_id, uint32_t throughput)
{
	algo_throughput[thr_id][opt_algo] = throughput;
}

void bench_display_results()
{
	for (int n=0; n < opt_n_threads; n++)
	{
		int dev_id = device_map[n];
		char gpu_model[64] = { 0 };
		strncpy(gpu_model, device_name[dev_id], sizeof(gpu_model) - 1);

		double total_rate = 0.0;
		double total_temp = 0.0;
		double total_power = 0.0;
		int algo_count = 0;

		applog(LOG_BLUE, "");
		applog(LOG_BLUE, "+------------+----------+------+-------+-------+-------+------------+");
		applog(LOG_BLUE, "| Algorithm  | Hashrate | Temp | Notes | Driver| Version| Efficiency |");
		applog(LOG_BLUE, "+------------+----------+------+-------+-------+-------+------------+");

		for (int i=0; i < ALGO_COUNT-1; i++) {
			double rate = algo_hashrates[n][i];
			if (rate == 0.0) continue;

			double kh_rate = rate / 1024.0;
			double mh_rate = kh_rate / 1024.0;
			double temp = algo_temps[n][i];
			double power = algo_power[n][i];
			double efficiency = (power > 0) ? kh_rate / power : 0.0;

			char rate_str[32] = { 0 };
			char eff_str[32] = { 0 };

			if (mh_rate >= 1000.0) {
				snprintf(rate_str, sizeof(rate_str), "%.2f GH/s", mh_rate / 1024.0);
			} else if (mh_rate >= 1.0) {
				snprintf(rate_str, sizeof(rate_str), "%.2f MH/s", mh_rate);
			} else {
				snprintf(rate_str, sizeof(rate_str), "%.2f kH/s", kh_rate);
			}

			if (efficiency >= 1000.0) {
				snprintf(eff_str, sizeof(eff_str), "%.2f MH/W", efficiency / 1024.0);
			} else {
				snprintf(eff_str, sizeof(eff_str), "%.2f kH/W", efficiency);
			}

			applog(LOG_INFO, "| %-10s | %8s | %3.0fC | %-5s | %-5s | %9s | %10s |",
				algo_names[i], rate_str, temp, "", driver_version, PACKAGE_VERSION, eff_str);

			total_rate += mh_rate;
			total_temp += temp;
			total_power += power;
			algo_count++;
		}

		applog(LOG_BLUE, "+------------+----------+------+-------+-------+-------+------------+");

		if (algo_count > 0) {
			double avg_rate = total_rate / algo_count;
			double avg_temp = total_temp / algo_count;
			double avg_power = total_power / algo_count;
			double avg_efficiency = (avg_power > 0) ? avg_rate / avg_power : 0.0;

			char avg_rate_str[32] = { 0 };
			char avg_eff_str[32] = { 0 };

			if (avg_rate >= 1000.0) {
				snprintf(avg_rate_str, sizeof(avg_rate_str), "%.2f GH/s", avg_rate / 1024.0);
			} else {
				snprintf(avg_rate_str, sizeof(avg_rate_str), "%.2f MH/s", avg_rate);
			}

			if (avg_efficiency >= 1000.0) {
				snprintf(avg_eff_str, sizeof(avg_eff_str), "%.2f MH/W", avg_efficiency / 1024.0);
			} else {
				snprintf(avg_eff_str, sizeof(avg_eff_str), "%.2f kH/W", avg_efficiency);
			}

			applog(LOG_BLUE, "| %-10s | %8s | %3.0fC | %-5s | %-5s | %9s | %10s |",
				"AVERAGE", avg_rate_str, avg_temp, "", driver_version, PACKAGE_VERSION, avg_eff_str);
			applog(LOG_BLUE, "+------------+----------+------+-------+-------+-------+------------+");
		}

		applog(LOG_BLUE, "GPU Model: %s", gpu_model);
		applog(LOG_BLUE, "");
	}
}

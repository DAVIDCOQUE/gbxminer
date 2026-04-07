#include <stdio.h>
#include <memory.h>
#include <string.h>
#include <unistd.h>
#include <map>

// include thrust
#ifndef __cplusplus
#include <thrust/version.h>
#include <thrust/remove.h>
#include <thrust/device_vector.h>
#include <thrust/iterator/constant_iterator.h>
#else
#include <ctype.h>
#endif

#include "miner.h"
#include "nvml.h"

#include "cuda_runtime.h"

#ifdef __cplusplus
/* miner.h functions are declared in C type, not C++ */
extern "C" {
#endif

// Occupancy-based dynamic launch configuration
// Returns the maximum number of active blocks per multiprocessor for a given kernel
static int cuda_occupancy_max_blocks_per_mp(void *kernel, int threads_per_block, size_t dynamic_smem_size, int dev_id)
{
	int max_active_blocks = 0;
	cudaError_t err = cudaOccupancyMaxActiveBlocksPerMultiprocessor(
		&max_active_blocks, kernel, threads_per_block, dynamic_smem_size);
	if (err != cudaSuccess) {
		// Fallback to a reasonable default if occupancy query fails
		return 0;
	}
	return max_active_blocks;
}

// Calculate optimal throughput based on device occupancy
// This replaces hardcoded intensity multipliers with dynamic calculation
uint32_t cuda_optimal_throughput(int thr_id, int threads_per_block, size_t dynamic_smem_size)
{
	int dev_id = device_map[thr_id % MAX_GPUS];
	cudaDeviceProp props;
	cudaError_t err = cudaGetDeviceProperties(&props, dev_id);
	if (err != cudaSuccess) {
		// Fallback to default
		return 0;
	}

	// Get device-specific maximums
	int max_threads_per_mp = props.maxThreadsPerMultiProcessor;
	int max_blocks_per_mp = props.maxBlocksPerMultiProcessor;
	int multiprocessor_count = props.multiProcessorCount;

	// Calculate optimal threads per SM based on threads per block
	// We want to maximize occupancy while respecting hardware limits
	int threads_per_sm = (max_threads_per_mp / threads_per_block) * threads_per_block;
	if (threads_per_sm > max_threads_per_mp) {
		threads_per_sm -= threads_per_block;
	}

	// Ensure we don't exceed max blocks per SM
	int blocks_per_sm = threads_per_sm / threads_per_block;
	if (blocks_per_sm > max_blocks_per_mp) {
		blocks_per_sm = max_blocks_per_mp;
		threads_per_sm = blocks_per_sm * threads_per_block;
	}

	// Total optimal throughput = threads per SM * number of SMs
	// Use a conservative 85% of theoretical maximum to leave room for OS/display GPU
	uint32_t optimal_throughput = (uint32_t)threads_per_sm * multiprocessor_count * 85 / 100;

	// Apply sanity bounds
	uint32_t min_throughput = threads_per_block * multiprocessor_count;
	uint32_t max_throughput = threads_per_block * max_blocks_per_mp * multiprocessor_count;

	if (optimal_throughput < min_throughput)
		optimal_throughput = min_throughput;
	if (optimal_throughput > max_throughput)
		optimal_throughput = max_throughput;

	return optimal_throughput;
}

// Legacy occupancy query function for external kernel use
int cuda_get_max_active_blocks_per_mp(void *kernel, int threads_per_block, size_t dynamic_smem_size, int dev_id)
{
	return cuda_occupancy_max_blocks_per_mp(kernel, threads_per_block, dynamic_smem_size, dev_id);
}

// CUDA Devices on the System
int cuda_num_devices()
{
	int version = 0, GPU_N = 0;
	cudaError_t err = cudaDriverGetVersion(&version);
	if (err != cudaSuccess) {
		applog(LOG_ERR, "Unable to query CUDA driver version! Is an nVidia driver installed?");
		exit(1);
	}

	if (version < CUDART_VERSION) {
		applog(LOG_ERR, "Your system does not support CUDA %d.%d API!",
			CUDART_VERSION / 1000, (CUDART_VERSION % 1000) / 10);
		exit(1);
	}

	err = cudaGetDeviceCount(&GPU_N);
	if (err != cudaSuccess) {
		applog(LOG_ERR, "Unable to query number of CUDA devices! Is an nVidia driver installed?");
		exit(1);
	}
	return GPU_N;
}

int cuda_version()
{
	return (int) CUDART_VERSION;
}

void cuda_devicenames()
{
	cudaError_t err;
	int GPU_N;
	err = cudaGetDeviceCount(&GPU_N);
	if (err != cudaSuccess)
	{
		applog(LOG_ERR, "Unable to query number of CUDA devices! Is an nVidia driver installed?");
		exit(1);
	}

	if (opt_n_threads)
		GPU_N = min(MAX_GPUS, opt_n_threads);
	for (int i=0; i < GPU_N; i++)
	{
		char vendorname[32] = { 0 };
		int dev_id = device_map[i];
		cudaDeviceProp props;
		cudaGetDeviceProperties(&props, dev_id);

		device_sm[dev_id] = (props.major * 100 + props.minor * 10);
		device_mpcount[dev_id] = (short) props.multiProcessorCount;

		if (device_name[dev_id]) {
			free(device_name[dev_id]);
			device_name[dev_id] = NULL;
		}
#ifdef USE_WRAPNVML
		if (gpu_vendor((uint8_t)props.pciBusID, vendorname) > 0 && strlen(vendorname)) {
			device_name[dev_id] = (char*) calloc(1, strlen(vendorname) + strlen(props.name) + 2);
			if (!strncmp(props.name, "GeForce ", 8))
				sprintf(device_name[dev_id], "%s %s", vendorname, &props.name[8]);
			else
				sprintf(device_name[dev_id], "%s %s", vendorname, props.name);
		} else
#endif
			device_name[dev_id] = strdup(props.name);
	}
}

void cuda_print_devices()
{
	int ngpus = cuda_num_devices();
	cuda_devicenames();
	for (int n=0; n < ngpus; n++) {
		int dev_id = device_map[n % MAX_GPUS];
		cudaDeviceProp props;
		cudaGetDeviceProperties(&props, dev_id);
		if (!opt_n_threads || n < opt_n_threads) {
			fprintf(stderr, "GPU #%d: SM %d.%d %s @ %.0f MHz (MEM %.0f)\n", dev_id,
				props.major, props.minor, device_name[dev_id],
				(double) props.clockRate/1000,
				(double) props.memoryClockRate/1000);
#ifdef USE_WRAPNVML
			if (opt_debug) nvml_print_device_info(dev_id);
#ifdef WIN32
			if (opt_debug) {
				unsigned int devNum = nvapi_devnum(dev_id);
				nvapi_pstateinfo(devNum);
			}
#endif
#endif
		}
	}
}

void cuda_shutdown()
{
	// require gpu init first
	//if (thr_info != NULL)
	//	cudaDeviceSynchronize();
	cudaDeviceReset();
}

static bool substringsearch(const char *haystack, const char *needle, int &match)
{
	int hlen = (int) strlen(haystack);
	int nlen = (int) strlen(needle);
	for (int i=0; i < hlen; ++i)
	{
		if (haystack[i] == ' ') continue;
		int j=0, x = 0;
		while(j < nlen)
		{
			if (haystack[i+x] == ' ') {++x; continue;}
			if (needle[j] == ' ') {++j; continue;}
			if (needle[j] == '#') return ++match == needle[j+1]-'0';
			if (tolower(haystack[i+x]) != tolower(needle[j])) break;
			++j; ++x;
		}
		if (j == nlen) return true;
	}
	return false;
}

// CUDA Gerät nach Namen finden (gibt Geräte-Index zurück oder -1)
int cuda_finddevice(char *name)
{
	int num = cuda_num_devices();
	int match = 0;
	for (int i=0; i < num; ++i)
	{
		cudaDeviceProp props;
		if (cudaGetDeviceProperties(&props, i) == cudaSuccess)
			if (substringsearch(props.name, name, match)) return i;
	}
	return -1;
}

// since 1.7
uint32_t cuda_default_throughput(int thr_id, uint32_t defcount)
{
	//int dev_id = device_map[thr_id % MAX_GPUS];
	uint32_t throughput = gpus_intensity[thr_id] ? gpus_intensity[thr_id] : defcount;
	if (gpu_threads > 1 && throughput == defcount) throughput /= (gpu_threads-1);
	if (api_thr_id != -1) api_set_throughput(thr_id, throughput);
	//gpulog(LOG_INFO, thr_id, "throughput %u", throughput);
	return throughput;
}

// since 1.8.3
double throughput2intensity(uint32_t throughput)
{
	double intensity = 0.;
	uint32_t ws = throughput;
	uint8_t i = 0;
	while (ws > 1 && i++ < 32)
		ws = ws >> 1;
	intensity = (double) i;
	if (i && ((1U << i) < throughput)) {
		intensity += ((double) (throughput-(1U << i)) / (1U << i));
	}
	return intensity;
}

// if we use 2 threads on the same gpu, we need to reinit the threads
void cuda_reset_device(int thr_id, bool *init)
{
	int dev_id = device_map[thr_id % MAX_GPUS];
	cudaSetDevice(dev_id);
	if (init != NULL) {
		// with init array, its meant to be used in algo's scan code...
		for (int i=0; i < MAX_GPUS; i++) {
			if (device_map[i] == dev_id) {
				init[i] = false;
			}
		}
		// force exit from algo's scan loops/function
		restart_threads();
		cudaDeviceSynchronize();
		while (cudaStreamQuery(NULL) == cudaErrorNotReady)
			usleep(1000);
	}
	cudaDeviceReset();
	if (opt_cudaschedule >= 0) {
		cudaSetDeviceFlags((unsigned)(opt_cudaschedule & cudaDeviceScheduleMask));
	} else {
		cudaSetDeviceFlags(cudaDeviceScheduleBlockingSync);
	}
	cudaDeviceSynchronize();
}

// return free memory in megabytes
int cuda_available_memory(int thr_id)
{
	int dev_id = device_map[thr_id % MAX_GPUS];
#if defined(_WIN32) && defined(USE_WRAPNVML)
	uint64_t tot64 = 0, free64 = 0;
	// cuda (6.5) one can crash on pascal and dont handle 8GB
	nvapiMemGetInfo(dev_id, &free64, &tot64);
	return (int) (free64 / (1024));
#else
	size_t mtotal = 0, mfree = 0;
	cudaSetDevice(dev_id);
	cudaDeviceSynchronize();
	cudaMemGetInfo(&mfree, &mtotal);
	return (int) (mfree / (1024 * 1024));
#endif
}

// Check (and reset) last cuda error, and report it in logs
void cuda_log_lasterror(int thr_id, const char* func, int line)
{
	cudaError_t err = cudaGetLastError();
	if (err != cudaSuccess && !opt_quiet)
		gpulog(LOG_WARNING, thr_id, "%s:%d %s", func, line, cudaGetErrorString(err));
}

// Clear any cuda error in non-cuda unit (.c/.cpp)
void cuda_clear_lasterror()
{
	cudaGetLastError();
}

#ifdef __cplusplus
} /* extern "C" */
#endif

int cuda_gpu_info(struct cgpu_info *gpu)
{
	cudaDeviceProp props;
	if (cudaGetDeviceProperties(&props, gpu->gpu_id) == cudaSuccess) {
		gpu->gpu_clock = (uint32_t) props.clockRate;
		gpu->gpu_memclock = (uint32_t) props.memoryClockRate;
		gpu->gpu_mem = (uint64_t) (props.totalGlobalMem / 1024); // kB
#if defined(_WIN32) && defined(USE_WRAPNVML)
		// required to get mem size > 4GB (size_t too small for bytes on 32bit)
		nvapiMemGetInfo(gpu->gpu_id, &gpu->gpu_memfree, &gpu->gpu_mem); // kB
#endif
		gpu->gpu_mem = gpu->gpu_mem / 1024; // MB
		return 0;
	}
	return -1;
}

// Zeitsynchronisations-Routine von cudaminer mit CPU sleep
// Note: if you disable all of these calls, CPU usage will hit 100%
typedef struct { double value[8]; } tsumarray;
cudaError_t MyStreamSynchronize(cudaStream_t stream, int situation, int thr_id)
{
	cudaError_t result = cudaSuccess;
	if (abort_flag)
		return result;
	if (situation >= 0)
	{
		static std::map<int, tsumarray> tsum;

		double a = 0.95, b = 0.05;
		if (tsum.find(situation) == tsum.end()) { a = 0.5; b = 0.5; } // faster initial convergence

		double tsync = 0.0;
		double tsleep = 0.95 * tsum[situation].value[thr_id];
		if (cudaStreamQuery(stream) == cudaErrorNotReady)
		{
			usleep((useconds_t)(1e6*tsleep));
			struct timeval tv_start, tv_end;
			gettimeofday(&tv_start, NULL);
			result = cudaStreamSynchronize(stream);
			gettimeofday(&tv_end, NULL);
			tsync = 1e-6 * (tv_end.tv_usec-tv_start.tv_usec) + (tv_end.tv_sec-tv_start.tv_sec);
		}
		if (tsync >= 0) tsum[situation].value[thr_id] = a * tsum[situation].value[thr_id] + b * (tsleep+tsync);
	}
	else
		result = cudaStreamSynchronize(stream);
	return result;
}

void cudaReportHardwareFailure(int thr_id, cudaError_t err, const char* func)
{
	struct cgpu_info *gpu = &thr_info[thr_id].gpu;
	gpu->hw_errors++;
	gpulog(LOG_ERR, thr_id, "%s %s", func, cudaGetErrorString(err));
	sleep(1);
}

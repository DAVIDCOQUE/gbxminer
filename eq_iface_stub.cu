/**
 * eq_iface_stub.cu
 *
 * Provides eq_cuda_context_interface::solve() vtable stub on Windows.
 *
 * On Linux/macOS equihash.cpp (g++) provides this stub with GCC ABI.
 * On Windows cuda_equi.cu is compiled by nvcc+cl.exe (MSVC ABI), so
 * all vtable references in cuda_equi.o use MSVC-mangled names.  The
 * stub must therefore also be compiled by cl.exe to produce the
 * matching MSVC-mangled symbol.  A separate .cu file achieves this
 * without touching cuda_equi.cu.
 */

#ifdef _WIN32

#include "eqcuda.hpp"

void eq_cuda_context_interface::solve(
    const char *tequihash_header,
    unsigned int tequihash_header_len,
    const char* nonce,
    unsigned int nonce_len,
    fn_cancel cancelf,
    fn_solution solutionf,
    fn_hashdone hashdonef)
{
    /* default no-op; concrete implementations are in eq_cuda_context<> */
}

#endif /* _WIN32 */

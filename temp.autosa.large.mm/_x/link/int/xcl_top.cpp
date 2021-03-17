#include "libspir_types.h"
#include "hls_stream.h"
#include "xcl_top_defines.h"
#include "ap_axi_sdata.h"
#define EXPORT_PIPE_SYMBOLS 1
#include "cpu_pipes.h"
#undef EXPORT_PIPE_SYMBOLS
#include "xcl_half.h"
#include <cstddef>
#include <vector>
#include <complex>
#include <pthread.h>
using namespace std;

extern "C" {

void kernel0(size_t A, size_t B, size_t C);

static pthread_mutex_t __xlnx_cl_kernel0_mutex = PTHREAD_MUTEX_INITIALIZER;
void __stub____xlnx_cl_kernel0(char **argv) {
  void **args = (void **)argv;
  size_t A = *((size_t*)args[0+1]);
  size_t B = *((size_t*)args[1+1]);
  size_t C = *((size_t*)args[2+1]);
 pthread_mutex_lock(&__xlnx_cl_kernel0_mutex);
  kernel0(A, B, C);
  pthread_mutex_unlock(&__xlnx_cl_kernel0_mutex);
}
}

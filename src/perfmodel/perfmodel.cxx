#include "perfmodel.hxx"

#include <cmath>
#include <cassert>
#include <array>

using namespace std;
using namespace perf;

namespace machine {
  // 10 TF CPU ( 5e11 FMA / second )
  double wflop_secs = (double) 1.0 / 5e11;
  // bytes per word
  int word_byte_n = 8;
  // cache line bytes
  int cl_byte_n = 64;
  // 256 KiB cache per thread
  int cache_byte_n = (1 << 18); 
  // 1 TiB/s memory bandwidth
  double byte_secs = (double) 1.0 / ((long long unsigned) 1 << 40);
  // write allocate flag
  bool flag_write_allocate = true;
}

// read and write characteristics of stencils (optimistic FMA and vectorized DP division)
namespace perf {
  bool flag_debug = false;
  const Stencil3DParams smooth { (11.+10)/2,
                                 {13./2, 10, 7, 4},
                                 { 1./2,  0, 0, 0},
                                 { 0   ,  1, 1, 1} };
  const Stencil3DParams apply { 11+10,
                                {13, 10, 7, 4},
                                { 1,  1, 1, 1},
                                { 0,  0, 0, 0} };
  // scaling factor fudge for large array inputs
  const Stencil3DParams restr { 7+10,
                                { 8, 4*2, 2*4, 1*8},
                                { 1, 1  , 1  , 1  },
                                { 0, 0  , 0  , 0  } };
  // scaling factor fudge for small array inputs
  const Stencil3DParams pc_prolong { 0,
                                     { 1, 1./2, 1./4, 1./8},
                                     { 0, 0   , 0   , 0   },
                                     { 1, 1   , 1   , 1   } };
  // scaling factor fudge for small array inputs
  const Stencil3DParams lin_prolong { 8,
                                      { 8, 4./2, 2./4, 1./8},
                                      { 0, 0   , 0   , 0   },
                                      { 1, 1   , 1   , 1   } };
};

namespace {
  // dim-dimensional slice of tile
  inline int slice_byte_n(const array<int, 3> & tile, int dim) {
    assert(0 <= dim && dim < 4);
    int slice_word_n = 1;
    for (int i = 0; i < dim; ++i) {
      slice_word_n *= tile[i];
    }
    int result = slice_word_n * machine::word_byte_n;
//  if (flag_debug) {
//    printf("slice_byte_n((%d, %d, %d), %d) = %d\n",
//           tile[0], tile[1], tile[2], dim, result);
//  }
    return result;
  }
  // working set for loop across first dim dimensions
  inline int ws_byte_n(const Stencil3DParams & p, const array<int, 3> & tile, int dim) {
    int result = (p.ro[dim] + p.wo[dim] + p.rw[dim]) * slice_byte_n(tile, dim);
    if (flag_debug) {
      printf("ws_byte_n((%d, %d, %d), %d) = %d\n",
             tile[0], tile[1], tile[2], dim, result);
    }
    return result;
  }

  // read traffic for loop across first dim dimensions
  inline int read_byte_n(const Stencil3DParams & p, const array<int, 3> & tile, int dim) {
    double wo = (machine::flag_write_allocate ? p.wo[dim] : 0);
    int result = (p.ro[dim] + wo + p.rw[dim]) * slice_byte_n(tile, dim);
    if (flag_debug) {
      printf("read_byte_n((%d, %d, %d), %d) = %d\n",
             tile[0], tile[1], tile[2], dim, result);
    }
    return result;
  }

  // write traffic for loop across first dim dimensions
  inline int write_byte_n(const Stencil3DParams & p, const array<int, 3> & tile, int dim) {
    int result = (p.wo[dim] + p.rw[dim]) * slice_byte_n(tile, dim);
    if (flag_debug) {
      printf("write_byte_n((%d, %d, %d), %d) = %d\n",
             tile[0], tile[1], tile[2], dim, result);
    }
    return result;
  }
};

namespace perf {
  // returns seconds to compute a local operation
  double compute_s(const Stencil3DParams & p, const array<int, 3> & tile) {
    double cpu_secs = p.wflops * (tile[0]*tile[1]*tile[2]) * machine::wflop_secs;
    int fit_dim = 0, slice_n = 1;
    while (fit_dim < 3 && ws_byte_n(p, tile, fit_dim) <= machine::cache_byte_n) {
      ++fit_dim;
    }
    for (int i = fit_dim; i < 3; ++i) {
      slice_n *= tile[i];
    }
    int total_slice_byte_n = read_byte_n(p, tile, fit_dim) +
                             write_byte_n(p, tile, fit_dim);
    double mem_secs = slice_n * total_slice_byte_n * machine::byte_secs;
    if (flag_debug) {
      printf("cpu_secs: %g s\n", cpu_secs);
      printf("dimension %d working set %d fits into cache %d\n",
             fit_dim-1, ws_byte_n(p, tile, fit_dim-1), machine::cache_byte_n);
      printf("mem_secs: %g s\n", mem_secs);
    }
    return max(mem_secs, cpu_secs);
  }

  // overload for a single size argument (assumes cube shape)
  double compute_s(const Stencil3DParams & p, int cell_n)
  {
    // use a cube approximation for shape of region
    int side_n = std::cbrt(cell_n);
    return compute_s(p, {side_n, side_n, side_n});
  }
};

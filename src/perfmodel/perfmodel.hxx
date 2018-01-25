#ifndef _dc397926_6573_4bed_bf9e_2dabf884246d
#define _dc397926_6573_4bed_bf9e_2dabf884246d

#include <array>

namespace perf {
  struct Stencil3DParams {
    double wflops; // weighted flops (normalized to cost of DP FMA)
    // read-only, write-only, read-write
    // # of (cells, pencils, planes, blocks)
    std::array<double, 4> ro, wo, rw;

    Stencil3DParams(double wflops=0,
                    std::array<double, 4> ro={0,0,0,0},
                    std::array<double, 4> wo={0,0,0,0},
                    std::array<double, 4> rw={0,0,0,0}) :
      wflops(wflops), ro(ro), wo(wo), rw(rw) { }
    
    static Stencil3DParams nop() {
      return Stencil3DParams();
    }
  };

  extern bool flag_debug;
  extern const Stencil3DParams smooth, apply, restr, pc_prolong, lin_prolong;

  double compute_s(const Stencil3DParams & p, const std::array<int, 3> & tile);
  double compute_s(const Stencil3DParams & p, int cell_n);
}

#endif

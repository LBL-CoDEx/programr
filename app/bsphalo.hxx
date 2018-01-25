#ifndef _147fda85_09d3_4ef6_881f_b5f04512abed
#define _147fda85_09d3_4ef6_881f_b5f04512abed

#include "reduce.hxx"
#include "amr/slab.hxx"

namespace programr {
namespace amr {

extern bool flag_bsp_halo;

inline Ex<Slab> bsp_halo(
  Ex<Slab> kid,
  Ex<Slab> par,
  int halo_n,
  int prolong_halo_n=0,
  std::string note={}
) {
  kid = slab_halo(kid, par, halo_n, prolong_halo_n, note);
  if (flag_bsp_halo) {
    IList<Ex<Slab>> kp = List<Ex<Slab>>::make({kid});
    kid = ex_reduce<bool, Slab>(ex_list<Slab>(kp), false, [=](bool dummy) { return kid; });
  }
  return kid;
}

inline Ex<Slab> bsp_halo2(
      Ex<Slab> kid,
      Ex<Slab> par0,
      Ex<Slab> par1,
      int halo_n, int prolong_halo_n=0,
      std::string note={}
    ) {
  kid = slab_halo2(kid, par0, par1, halo_n, prolong_halo_n, note);
  if (flag_bsp_halo) {
    IList<Ex<Slab>> kp = List<Ex<Slab>>::make({kid});
    kid = ex_reduce<bool, Slab>(ex_list<Slab>(kp), false, [=](bool dummy) { return kid; });
  }
  return kid;
}
  
}}

#endif

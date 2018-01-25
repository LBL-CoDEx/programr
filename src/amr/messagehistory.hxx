#ifndef _936c0ef1_34ab_4401_9327_501ee1f8ccf1
#define _936c0ef1_34ab_4401_9327_501ee1f8ccf1

# include "data.hxx"
# include "lowlevel/weakset.hxx"
# include "lowlevel/ref.hxx"

# include <tuple>

namespace programr {
  struct MessageHistory {
    // box interior messages to self
    typedef std::tuple<
        int/*level*/,
        Imm<BoxList>/*boxes*/,
        RefWeak<Data>/*src_data*/,
        ImmBoxed<int[]>/*dst_ranks*/
      > IntrFromSelf;
    
    WeakSet<IntrFromSelf> intr_from_self;
    
    // box halo messages from parent to kid
    typedef std::tuple<
        std::uint8_t/*halo*/,
        std::uint8_t/*interp_halo*/,
        std::uint16_t/*src_level*/,
        RefWeak<Data>/*src_data*/,
        Imm<BoxList>/*src_boxes*/,
        Imm<BoxList>/*dst_boxes*/,
        ImmBoxed<int[]>/*dst_ranks*/
      > HaloFromPar;
    
    WeakSet<HaloFromPar> halo_from_par;
    
    // halo messages from sibling
    typedef std::tuple<
        std::uint8_t/*halo*/,
        std::uint16_t/*level*/,
        Imm<BoxList>/*boxes*/,
        RefWeak<Data>/*src_data*/,
        ImmBoxed<int[]>/*dst_ranks*/
      > HaloFromSib;
    
    WeakSet<HaloFromSib> halo_from_sib;
    
    // prolong
    typedef std::tuple<
        std::uint8_t/*interp_halo*/,
        std::uint16_t/*src_level*/,
        RefWeak<Data>/*src_data*/,
        Imm<BoxList>/*src_boxes*/,
        Imm<BoxList>/*dst_boxes*/,
        ImmBoxed<int[]>/*dst_ranks*/
      > ProlongFromPar;
    
    WeakSet<ProlongFromPar> prolong_from_par;
    
    // restrict
    typedef std::tuple<
        int/*src_level*/,
        RefWeak<Data>/*src_data*/,
        Imm<BoxList>/*src_boxes*/,
        Imm<BoxList>/*dst_boxes*/,
        ImmBoxed<int[]>/*dst_ranks*/
      > RestrictFromKid;
    
    WeakSet<RestrictFromKid> restrict_from_kid;
  };
}

#endif

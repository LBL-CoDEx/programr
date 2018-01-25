#ifndef _310bd1cb_aa2d_4c7a_bbeb_981a0d517124
#define _310bd1cb_aa2d_4c7a_bbeb_981a0d517124

# include "boundary.hxx"
# include "boxlist.hxx"
# include "boxmap.hxx"
# include "list.hxx"
# include "lowlevel/byteseq.hxx"
# include "lowlevel/intset.hxx"

# include <tuple>

namespace programr {
namespace amr {
namespace boxtree {
  struct Level {
    Imm<BoxList> boxes;
    int cell_scale_log2;
    int box_scale_log2;
    
    int unit_per_cell_log2() const {
      return box_scale_log2 - cell_scale_log2;
    }
    
    friend bool operator==(const Level &a, const Level &b) {
      return
        a.cell_scale_log2 == b.cell_scale_log2 &&
        a.box_scale_log2 == b.box_scale_log2 &&
        a.boxes == b.boxes;
    }

    Imm<BoxMap<Pt<int>>> geom_centers(int target_scale_log2) const {
      return BoxMap<Pt<int>>::make_by_ix_box(boxes, [=](int ix, const Box &b) {
        return (b.lo + b.hi) << (target_scale_log2 - box_scale_log2 - 1);
      });
    }
  };
  
  std::tuple<std::vector<Level>,Box>
  make_octree_full(
    int lev_n,
    int size
  );
  
  std::tuple<std::vector<Level>,Box>
  make_telescope(
    int lev_n,
    int dim_box_n,
    int box_size
  );
  
  Level coarsened(const Level &lev, int factor_log2);
  
  ByteSeqPtr siblings(const Level &lev, int ix, Boundary *bdry);
  ByteSeqPtr parents(const Level &kids, const Level &pars, int kid_ix, Boundary *bdry, bool neighboring);
  ByteSeqPtr children(const Level &kids, const Level &pars, int par_ix);
  
  // does not list `kid_ix` in output dependencies
  std::vector<std::tuple<int/*lev=0,-1*/,int/*ix*/,Box>>
  deps_halo(
    const Level &kids,
    const Level *pars/*nullable*/,
    int kid_ix,
    int halo,
    Boundary *bdry,
    int prolong_halo,
    bool flag_faces_only = true
  );
#if 0  
  std::vector<std::tuple<
  sats_halo(
    const Level &kids,
    const Level *pars/*nullable*/,
    int kid_ix,
    int halo,
    Boundary *bdry,
    int prolong_halo
  );
#endif
  std::vector<std::pair<int/*box_ix*/,Box>>
  deps_restrict(
    const Level &kids,
    const Level &pars,
    int par_ix
  );
  
  std::vector<std::pair<int/*box_ix*/,Box>>
  deps_prolong(
    const Level &kids,
    const Level &pars,
    int kid_ix,
    Boundary *bdry,
    int interp_halo
  );
}}}

namespace std {
  template<>
  struct hash<programr::amr::boxtree::Level> {
    size_t operator()(const programr::amr::boxtree::Level &x) const {
      return programr::hash(x.boxes, x.cell_scale_log2, x.box_scale_log2);
    }
  };
}
#endif

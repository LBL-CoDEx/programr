#ifndef _58803f1e_eed8_42ba_ba19_876d508ab8c4
#define _58803f1e_eed8_42ba_ba19_876d508ab8c4

# include "amr/boundary.hxx"
# include "amr/boxtree.hxx"
# include "amr/slab.hxx"
# include "list.hxx"

namespace programr {
  struct LevelAndRanks {
    amr::boxtree::Level level;
    Imm<amr::BoxMap<int>> rank_map;
  };
  
  Ref<Expr> main_ex(Ref<amr::Boundary> bdry, IList<LevelAndRanks> tree);
}

namespace std {
  template<>
  struct hash<programr::LevelAndRanks> {
    std::size_t operator()(const programr::LevelAndRanks &x) const {
      return programr::hash(x.level, x.rank_map);
    }
  };
}

#endif

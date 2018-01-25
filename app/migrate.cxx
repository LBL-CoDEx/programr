#include "app.hxx"

using namespace programr;
using namespace programr::amr;
using namespace std;

Ref<Expr> programr::main_ex(Ref<Boundary> bdry, IList<LevelAndRanks> tree) {
  auto x = tree->map_ix<Ex<Slab>>(
    [&](int lev, const LevelAndRanks &x) {
      return slab_literal(
        /*bdry*/bdry,
        /*level*/x.level,
        /*rank_map*/x.rank_map,
        /*elmt_sz*/sizeof(double),
        "init:literal"
      );
    }
  );

  auto y = x->map_ix<Ex<Slab>>(
    [&](int lev, const Ex<Slab> &x_lev) {
      auto new_ranks = BoxMap<int>::make_by_ix_box(
        x_lev.result()->level.boxes,
        [&](int ix, Box box) {
          return 0;
        }
      );
      return slab_migrate(x_lev, new_ranks, "migrate");
    }
  );

  return ex_list( y );
}

#include "app.hxx"
#include "multigrid.hxx"

using namespace programr;
using namespace programr::amr;
using namespace programr::amr::boxtree;
using namespace multigrid;
using namespace std;

Ref<Expr> programr::main_ex(
    Ref<Boundary> bdry,
    IList<LevelAndRanks> tree
  ) {
  
  tree = tree->reverse(); // finest first
  
  auto make_lit = [&]() {
    return tree->map_ix<Ex<Slab>>(
      [&](int lev, const LevelAndRanks &x) {
        return slab_literal(
          /*bdry*/bdry,
          /*level*/x.level,
          /*rank_map*/x.rank_map,
          /*elmt_sz*/sizeof(double)
        );
      }
    );
  };
  
  auto rhs = make_lit();
  
  return ex_list(mg_solve(rhs, nullptr));
}

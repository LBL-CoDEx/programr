#include "app.hxx"
#include "amr/boxtree.hxx"
#include "amr/slab.hxx"

#include <tuple>

using namespace programr;
using namespace programr::amr;
using namespace std;

Ref<Expr> programr::main_ex(Ref<Boundary> bdry, IList<LevelAndRanks> tree) {
  LevelAndRanks kid = tree->operator[](tree->size()-1);
  LevelAndRanks par = tree->operator[](tree->size()-2);
  Ex<Slab> a = slab_literal(bdry, kid.level, BoxMap<int>::make_constant(kid.level.boxes, 0), sizeof(double));
  Ex<Slab> b = slab_literal(bdry, kid.level, BoxMap<int>::make_constant(kid.level.boxes, 1), sizeof(double));
  Ex<Slab> c = slab_literal(bdry, par.level, BoxMap<int>::make_constant(par.level.boxes, 2), sizeof(double));
  return slab_stencil(slab_halo(slab_op({a, b}), c, 1), 1);
}

#include "app.hxx"
#include "amr/boxtree.hxx"
#include "amr/slab.hxx"
#include "reduce.hxx"

#include <tuple>

using namespace programr;
using namespace programr::amr;
using namespace std;

Ex<Slab> converge(
    Ex<Slab> x,
    const function<Ex<Slab>(Ex<Slab>)> &f,
    double s=1.0
  ) {
  
  return ex_reduce<double,Slab>(x, s,
    [=](const double &t) {
      if(t < .01)
        return x;
      else
        return converge(f(x), f, 0.5*t);
    }
  );
}

Ref<Expr> programr::main_ex(Ref<Boundary> bdry, IList<LevelAndRanks> tree) {
  Ex<Slab> a = slab_literal(bdry, tree->head().level, tree->head().rank_map, sizeof(double));
  Ex<Slab> b = slab_literal(bdry, tree->head().level, tree->head().rank_map, sizeof(double));
  
  return slab_op({
    converge(a, [=](Ex<Slab> x) { return slab_op({x, b}); }),
    converge(b, [=](Ex<Slab> x) { return slab_op({x, a}); })
  });
}

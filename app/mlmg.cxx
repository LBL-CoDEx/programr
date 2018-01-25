#include "app.hxx"
#include "env.hxx"
#include "list.hxx"
#include "multigrid.hxx"
#include "bsphalo.hxx"
#include "perfmodel/perfmodel.hxx"

#include <functional>
#include <tuple>
#include <sstream>

using namespace programr;
using namespace programr::amr;
using namespace multigrid;
using namespace std;

inline string with_time(string note, int time) {
  ostringstream ss;
  ss << note << " time=" << time;
  return ss.str();
}

IList<Ex<Slab>> step(
    int t0, int t1,
    MGType mg_type,
    IList<Ex<Slab>> xs
  ) {

  // advance in time
  IList<Ex<Slab>> par_xs = cons(nullptr, xs); // create parents list
  xs = xs->map_ix<Ex<Slab>>(
    [&](int lev, Ex<Slab> x) {
      auto par_x = (*par_xs)[lev];
      x = bsp_halo(x, par_x, ADVANCE_HALO, PROLONG_HALO,
                   with_time("step:advance:halo", t0));
      // TODO: add a perf model for advance
      return slab_stencil(x, ADVANCE_HALO,
                          with_time("step:advance:stencil", t1));
    }
  );

  auto compute_rhs = [](const Ex<Slab> &x) {
    return slab_op({x}, -1, "compute_rhs:op");
  };

  // do a multi-level MG solve across all levels
  auto bs = xs->map<Ex<Slab>>(compute_rhs);
  xs = mg_solve(mg_type, bs->reverse(), nullptr)->reverse();

  return xs;
}

Ref<Expr> programr::main_ex(Ref<Boundary> bdry, IList<LevelAndRanks> tree) {
  auto x = tree->map<Ex<Slab>>(
    [&](const LevelAndRanks &x) {
      return slab_literal(
        /*bdry*/bdry,
        /*level*/x.level,
        /*rank_map*/x.rank_map,
        /*elmt_sz*/sizeof(double),
        "init:literal"
      );
    }
  );

  MGType mg_type; {
    string mgt = env<string>("mgtype", "v");
    mg_type = mgt.c_str()[0] == 'f' ? MGType::fcycle : MGType::vcycle;
  }

  const int cstep_n = 1, cstep = 1<<(tree->size()-1), fstep = 1;
  const int t_beg = 0, t_end = cstep_n * cstep;
  for (int t = t_beg; t < t_end; t += fstep) {
    x = step(t, t+fstep, mg_type, x);
  }
  return ex_list(x);
}

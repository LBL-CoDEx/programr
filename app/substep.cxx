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

// synchronize across levels after each single level solve
bool flag_mg_sync = env<bool>("mgsync", false);
// use explicit time stepping (don't do multigrid, just advance)
bool flag_explicit = env<bool>("explicit", false);

inline string with_time(string note, int time) {
  ostringstream ss;
  ss << note << " time=" << time;
  return ss.str();
}

Imm<List<Ex<Slab>>> step(
    int lev, int lev_ub, int t0, int t2,
    MGType mg_type,
    const Imm<List<Ex<Slab>>> &x0,
    const Ex<Slab> par_x,
    bool flag_coarse_aligned = false
  ) {

  Say() << "step() called for level " << lev << " for time [" << t0 << ", " << t2 << "]";

  auto x0_h = x0->head();

  x0_h = bsp_halo(x0_h, par_x, ADVANCE_HALO, PROLONG_HALO,
                  with_time("step:advance:halo", t0));
  // TODO: add a perf model for advance
  auto x2_h = slab_stencil(x0_h, ADVANCE_HALO,
                           with_time("step:advance:stencil", t2));

  // used later to set up the rhs for the implicit solves
  auto compute_rhs = [](const Ex<Slab> &x) {
    return slab_op({x}, -1, "compute_rhs:op");
  };

  if (!flag_explicit) {
    // do a single-level MG solve at current level
    auto rhs_h = compute_rhs(x2_h);
    x2_h = mg_solve(mg_type, List<Ex<Slab>>::make({rhs_h}), par_x)->head();
  }

  if (lev + 1 == lev_ub)
    return List<Ex<Slab>>::make({x2_h});
  else {
    auto x0_t = x0->tail();
    int t1 = (t0+t2)/2;

    // implicit: both substeps require a mix of x0_h and x2_h
    // explicit: only second substep requires a mix of x0_h and x2_h

    // parent cells needed for second half step
    // mix from x0_h,x2_h to create time-interpolated boundary values
    // TODO: we actually only need values for the boundary of x0_t->head()
    auto mixed_x1_h = slab_op({x0->head(), x2_h}, -1,
                              with_time("step:time_interpolate:op", t1), 11.0); // + and /
    // parent cells needed for first half step
    auto x1_h = flag_explicit ? x0->head() : mixed_x1_h;

    // step first half
    auto x1_t = step(lev+1, lev_ub, t0, t1, mg_type, x0_t, x1_h);

    // step second half
    auto x2_t = step(lev+1, lev_ub, t1, t2, mg_type, x1_t, mixed_x1_h, true);

    // restrict
    x2_h = slab_restrict_reflux(x2_h, x2_t->head(), with_time("step:restrict_reflux", t2), perf::restr);

    auto x2 = cons(x2_h, x2_t);

    // skip MG sync if we're doing an explicit solve
    //   if time step is coarse aligned, some coarser level will call a multi-level
    //   mg_solve so we don't have to
    if (!flag_explicit && flag_mg_sync && !flag_coarse_aligned) {
      auto b = x2->map<Ex<Slab>>(compute_rhs);
      // synchronize across this and finer levels
      x2 = mg_solve(mg_type, b->reverse(), par_x)->reverse();
    }

    return x2;
  }
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

  int t_beg = 0, t_end = 1<<(tree->size()-1);
  return ex_list( step( 0, tree->size(), t_beg, t_end, mg_type, x, nullptr) );
}

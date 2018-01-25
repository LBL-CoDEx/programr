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

  // determine which levels participate in the solve
  int mg_lvl_lo = env<int>("mglo", 0);
  int mg_lvl_hi = env<int>("mghi", x->size());
  //Say() << "Solving MG levels: " << mg_lvl_lo << " to " << mg_lvl_hi;
  USER_ASSERT(mg_lvl_lo >= 0 &&
              mg_lvl_lo < mg_lvl_hi &&
              (size_t) mg_lvl_hi <= x->size(),
              "invalid MG level selection");

  // create sublist of participating levels
  // x is ordered coarse to fine
  // subxr is sublist of x, reversed (ordered fine to coarse)
  IList<Ex<Slab>> subxr = List<Ex<Slab>>::nil();
  {
    // skip to coarsest participating level
    auto p = x;
    for (int i = 0; i < mg_lvl_lo; ++i) {
      p = p->tail();
    }
    // add participating levels onto subxr
    for (int i = mg_lvl_lo; i < mg_lvl_hi; ++i) {
      subxr = cons(p->head(), subxr);
      p = p->tail();
    }
  }

  // mg_solve expects and returns a list ordered fine to coarse
  auto result = mg_solve(mg_type, subxr, nullptr);
  result = result->reverse(); // re-order coarse to fine

  return ex_list(result);
}

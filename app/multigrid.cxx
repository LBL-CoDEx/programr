#include "env.hxx"
#include "app.hxx"
#include "amr/slab.hxx"
#include "list.hxx"
#include "reduce.hxx"
#include "multigrid.hxx"
#include "bsphalo.hxx"
#include "perfmodel/perfmodel.hxx"

#include <functional>
#include <tuple>

using namespace programr;
using namespace programr::amr;
using namespace programr::amr::boxtree;
using namespace std;

namespace multigrid {

size_t solve_iter_n = env<size_t>("mgiters", 1);
size_t bottom_solve_iter_n = env<size_t>("mgbsiters", 10);
bool flag_bcgs = env<bool>("bcgs", true);
bool flag_cons = env<bool>("cons", false);
size_t cons_factor = env<size_t>("consfac", 8);

Ex<Slab> converge(
    Ex<Slab> x,
    Ex<Slab> res, // pass nullptr if not available
    const function<Ex<Slab>(Ex<Slab>)> &res_f,
    const function<Ex<Slab>(Ex<Slab>)> &iter_f,
    size_t iter_n=10.0
  ) {
  res = res ? res : res_f(x);
  // treat iter_n fake convergence result
  double d_iter_n = static_cast<double>(iter_n);
  // pretend we compute s from residual
  return ex_reduce<double,Slab>(res, d_iter_n,
    [=](const double &i)->Ex<Slab> {
      if (i <= 0) {
        return x;
      } else {
        // iter_f(x) reduces t
        return converge(iter_f(x), nullptr, res_f, iter_f, i-1);
      }
    }
  );
}

Ex<ExList<Slab>> converge_list_helper(
    IList<Ex<Slab>> x,
    IList<Ex<Slab>> res, // pass nullptr if not available
    const function<IList<Ex<Slab>>(IList<Ex<Slab>>)> &res_f,
    const function<IList<Ex<Slab>>(IList<Ex<Slab>>)> &iter_f,
    size_t iter_n
  ) {
  res = res ? res : res_f(x);
  // treat iter_n fake convergence result
  double s = static_cast<double>(iter_n);
  // pretend we compute s from residual
  return ex_reduce<double,ExList<Slab>>(
    ex_list(res), s,
    [=](const double &t)->Ex<ExList<Slab>> {
      if(t <= 0)
        return ex_list(x);
      else {
        auto x1 = iter_f(x);
        DEV_ASSERT(x1->size() == x->size());
        return converge_list_helper(x1, nullptr, res_f, iter_f, t-1);
      }
    }
  );
}

IList<Ex<Slab>> converge_list(
    IList<Ex<Slab>> x,
    IList<Ex<Slab>> res, // pass nullptr if not available
    const function<IList<Ex<Slab>>(IList<Ex<Slab>>)> &res_f,
    const function<IList<Ex<Slab>>(IList<Ex<Slab>>)> &iter_f,
    size_t iter_n=10
  ) {
  return ex_list_items(converge_list_helper(x, res, res_f, iter_f, iter_n), x->size());
}

Ex<Slab> coarsen(Ex<Slab> x) {
  return ex_meta<Slab>({x},
    [=](const MetaEnv &meta_env)->Ex<Slab> {
      Slab *meta = meta_env[x];
      auto coarse_level = boxtree::coarsened(meta->level, 1);
      auto rank_map = BoxMap<int>::make_by_ix(coarse_level.boxes,
        [&] (int ix) { return (*meta->rank_map)((*meta->level.boxes)[ix]); }
      );
      Ex<Slab> par = slab_literal(
        meta->bdry,
        coarse_level,
        rank_map,
        meta->elmt_sz,
        "coarsen:literal"
      );
      return slab_restrict(par, x, false, "coarsen:restrict", perf::restr);
    }
  );
}

Ex<Slab> slab_zero_like(Ex<Slab> like) {
  return ex_meta<Slab>({like},
    [=](const MetaEnv &meta_env)->Ex<Slab> {
      Slab *meta = meta_env[like];
      return slab_literal(meta->bdry, meta->level, meta->rank_map, meta->elmt_sz, "zero:literal");
    }
  );
}

Ex<Slab> residual(Ex<Slab> x, Ex<Slab> rhs, Ex<Slab> par_x=nullptr) {
  if (x == nullptr) return rhs;
  Ex<Slab> x_h = bsp_halo(x, par_x, MG_HALO, PROLONG_HALO, "residual:halo");
  Ex<Slab> x_a = slab_stencil(x_h, MG_HALO, "residual:stencil", perf::apply);
  return slab_op({rhs, x_a}, -1, "residual:op", 1.0); // return rhs-x_a
}

Ex<Slab> relax(int iter_n, Ex<Slab> rhs, Ex<Slab> x=nullptr, Ex<Slab> par_x=nullptr) {
  x = x ? x : slab_zero_like(rhs); // zero
  for(int i=0; i < iter_n; i++) {
    x = bsp_halo(x, par_x, MG_HALO, 0, "relax:halo");
    x = slab_stencil(x, MG_HALO, "relax:stencil", perf::smooth);
    x = slab_op({rhs, x}, -1, "relax:op", 1.0); // return rhs-x
  }
  return x;
}

// return x + prolongate(u)
// if x is nullptr, then use x_like to determine the shape of the result
Ex<Slab> prolong_update(
    Ex<Slab> x, Ex<Slab> u,
    Ex<Slab> x_like=nullptr,
    bool flag_pc = true
  ) {
  int halo_width           = flag_pc ? 0 : PROLONG_HALO;
  perf::Stencil3DParams sp = flag_pc ? perf::pc_prolong : perf::lin_prolong;
  if (x == nullptr) {
    DEV_ASSERT(x_like != nullptr);
    return slab_prolong(x_like, u, halo_width, "prolong_update:prolong", sp);
  } else {
    auto u_p = slab_prolong(x, u, halo_width, "prolong_update:prolong", sp);
    return slab_op({x, u_p}, -1, "prolong_update:update", 1.0); // return x+u_p
  }
}

Ex<Slab> mg_bottom_solve_gsrb(
    Ex<Slab> rhs,
    Ex<Slab> x = nullptr
  ) {
  if (x == nullptr) x = slab_zero_like(rhs);
  return converge(x, rhs,
    [=](Ex<Slab> x) { return residual(x, rhs); },
    [=](Ex<Slab> x) { return relax(2, rhs, x); }, // single GSRB relaxation
    bottom_solve_iter_n
  );
}

// TODO: replace this with something that returns Ex<ScalarType>
//       instead of a replicated value over Slab
Ex<Slab> dot_prod(Ex<Slab> a, Ex<Slab> b, string note_prefix="") {
  double dummy = 0.0;
  auto c = slab_op({a, b}, -1, note_prefix+"dot_prod:op", 1.0); // c=a*b
  return ex_reduce<double,Slab>(c, dummy,
    [=](const double &d) {
      return c; // pretend c gets the result value replicated over slab
    }
  );
}

Ex<Slab> bicgstab_converge(
    Ex<Slab> x,
    Ex<Slab> r0,
    Ex<Slab> r,
    Ex<Slab> p,
    Ex<Slab> r_dot_r0, // can pass nullptr
    double d_iter_n
  ) {
  // compute dot products
  if (!r_dot_r0) r_dot_r0 = dot_prod(r, r0, "bcgs:r_dot_r0:");
  auto Ap = bsp_halo(p, nullptr, BCGS_HALO, 0, "bcgs:Ap:halo");
  Ap = slab_stencil(Ap, BCGS_HALO, "bcgs:Ap:stencil", perf::apply);
  auto Ap_dot_r0 = dot_prod(Ap, r0, "bcgs:Ap_dot_r0:");

  // update x and s
  auto alpha = slab_op({r_dot_r0, Ap_dot_r0}, -1, "bcgs:alpha:op", 0.0); // scalar div
  auto x_new = slab_op({x, alpha, p}, -1, "bcgs:x_new:op", 2.0); // x += alpha*p
  auto s = slab_op({r, alpha, Ap}, -1, "bcgs:s:op", 2.0); // s = r - alpha*Ap

  // reduce over s to compute norm(s) to check convergence
  // hack: iter_n as fake convergence result
  return ex_reduce<double,Slab>(s, d_iter_n,
    [=](const double &i1)->Ex<Slab> {

      // in real code, return x_new if norm(s) is small
      // if (norm(s) <= threshold) return x_new;

      // compute dot products
      auto As = bsp_halo(s, nullptr, BCGS_HALO, 0, "bcgs:As::halo");
      As = slab_stencil(As, BCGS_HALO, "bcgs:As:stencil", perf::apply);
      auto As_dot_s = dot_prod(As, s, "bcgs:As_dot_s:");
      auto As_dot_As = dot_prod(As, As, "bcgs:As_dot_As:");

      // update x and r
      auto omega = slab_op({As_dot_s, As_dot_As}, -1, "bcgs:omega:op", 0.0); // scalar div
      auto x_new2 = slab_op({x_new, omega, s}, -1, "bcgs:x_new2:op", 2.0); // x += omega*s
      auto r = slab_op({s, omega, As}, -1, "bcgs:r:op", 2.0); // r = s - omega*As

      // reduce over r to compute norm(r) to check convergence
      // hack: i1 (iter_n) as fake convergence result
      return ex_reduce<double,Slab>(r, i1-1,
        [=](const double &i2)->Ex<Slab> {

          // in real code, return x_new2 if norm(r) is small
          // if (norm(r) <= threshold) return x_new2;
          if (i2 <= 0) {
            return x_new2;
          } else {
            auto r_dot_r0_new = dot_prod(r, r0, "bcgs:r_dot_r0:");
            // scalar ops: beta = (r_dot_r0_new / r_dot_r0) * (alpha / omega)
            auto beta = slab_op({r_dot_r0_new, r_dot_r0, alpha, omega},
                                -1, "bcgs:beta:op", 0.0);
            // p = r + beta*(p - omega*Ap)
            auto p_new = slab_op({r, beta, p, omega, Ap}, -1, "bcgs:p_new:op", 4.0);
            return bicgstab_converge(x_new2, r0, r, p_new, r_dot_r0_new, i2);
          }
        }
      );
    }
  );
}

Ex<Slab> mg_bottom_solve_bcgs(
    Ex<Slab> rhs,
    Ex<Slab> x // can pass in nullptr
  ) {
  auto r0 = residual(x, rhs);
  double d_iter_n = static_cast<double>(bottom_solve_iter_n);
  if (x == nullptr) x = slab_zero_like(rhs); // ensure x is instantiated

  // reduce over r0 to compute norm(r0) to check initial convergence
  // hack: d_iter_n as fake convergence result
  return ex_reduce<double,Slab>(r0, d_iter_n,
    [=](const double &i) {
      if (i <= 0) {
        return x; // already converged
      } else {
        return bicgstab_converge(x, r0, r0, r0, nullptr, i); // recursive iteration
      }
    }
  );
}

// consolidate by multigrid::cons_factor
Ex<Slab> consolidate( Ex<Slab> x ) {
  if (!flag_cons) return x;
  return ex_meta<Slab>({x},
    [=](const MetaEnv &env)->Ex<Slab> {
      Slab *meta = env[x];
      auto old_ranks = meta->rank_map;
//    Say() << "consolidate called on level " << meta->level.cell_scale_log2;
//    for(int i=0; i < (int)meta->level.boxes->size(); i++) {
//      Box b = (*meta->level.boxes)[i];
//      Box p = (*meta->level.boxes)[i/cons_factor];
//      int r_old = (*old_ranks)(b);
//      int r_new = (*old_ranks)(p);
//      Say() << "  (box, old rank -> new rank): (" << b << ", " << r_old << " -> " << r_new << ")";
//    }
      auto new_ranks = BoxMap<int>::make_by_ix(
        meta->level.boxes,
        [=] (int ix) { return (*old_ranks)((*meta->level.boxes)[ix/cons_factor]); }
      );
      return slab_migrate(x, new_ranks, "consolidate");
  });
}


Ex<Slab> deconsolidate( Ex<Slab> x, Ex<Slab> old ) {
  if (!flag_cons) return x;
  return ex_meta<Slab>({old},
    [=](const MetaEnv &env)->Ex<Slab> {
      Slab *meta = env[old];
      auto old_ranks = meta->rank_map;
//    Say() << "deconsolidate called on level " << meta->level.cell_scale_log2;
//    for(int i=0; i < (int)meta->level.boxes->size(); i++) {
//      Box b = (*meta->level.boxes)[i];
//      int r = (*old_ranks)(b);
//      Say() << "  (box, rank): (" << b << ", " << r << ")";
//    }
      return slab_migrate(x, old_ranks, "deconsolidate");
  });
}


// Find x : A*x = rhs.

// solve on a single AMR level
Ex<Slab> mg_single_level(
    MGType mg_type,
    Ex<Slab> rhs,
    Ex<Slab> prev_x // can pass in nullptr
  ) {
  
  return ex_meta<Slab>({rhs},
    [=](const MetaEnv &env)->Ex<Slab> {
      Slab *meta = env[rhs];
      
      bool base = false;
      for(int i=0; i < (int)meta->level.boxes->size(); i++) {
        Pt<int> size = (*meta->level.boxes)[i].size();
        if((size & 1) != 0 || any_le(size, Pt<int>(MG_COARSEN_MIN))) {
          base = true;
          break;
        }
      }
      
      auto x = prev_x;

      if(base) {
        // bottom solve
        if (flag_bcgs) {
          x = mg_bottom_solve_bcgs(rhs, x);
        } else {
          x = mg_bottom_solve_gsrb(rhs, x);
        }
      }
      else if (mg_type == MGType::vcycle) {
        x = relax(NU, rhs, x);       // x = relax(A*x = rhs)
        auto res = residual(x, rhs); // res = rhs - A*x
        // recurse V-cycle
        auto cres = coarsen(res);
        auto con_cres = consolidate(cres);
        auto con_u = mg_single_level(MGType::vcycle, con_cres, nullptr);
        auto u = deconsolidate(con_u, cres);
        x = prolong_update(x, u);    // x += prolong(u)
        x = relax(NU, rhs, x);    // x = relax(A*x = rhs)
      }
      else if (mg_type == MGType::fcycle) {
        auto res = residual(x, rhs); // res = rhs - A*x
        // recurse F-cycle
        auto cres = coarsen(res);
        auto con_cres = consolidate(cres);
        auto con_u = mg_single_level(MGType::fcycle, con_cres, nullptr);
        auto u = deconsolidate(con_u, cres);
        x = prolong_update(x, u, rhs, false); // x += prolong(u)
        x = mg_single_level(MGType::vcycle, rhs, x); // V-cycle
      } else {
        USER_ASSERT(false, "Invalid MGType passed to mg_single_level");
      }

      return x;
    }
  );
}

// solve across multiple AMR levels (composite MG solve)
// accepts a guess x and returns better x
// lists are in resolution-descending order
Imm<List<Ex<Slab>>> amr_mg_cycle(
    MGType mg_type,
    Imm<List<Ex<Slab>>> rhs,
    Imm<List<Ex<Slab>>> x,
    Ex<Slab> bottom_par_x, // parent of the coarsest level in the x list
    function<Ex<Slab>(Ex<Slab>)> res_fixup = [](Ex<Slab> x) { return x; }
  ) {
  auto top_rhs = rhs->head();
  auto tail_rhs = rhs->tail();
  auto top_x = x->head();
  auto tail_x = x->tail(); // List<Ex<Slab>>::nil() if x.size()==1
  auto par_x0 = rhs->size()==1 ? bottom_par_x : tail_x->head(); // get coarse parent

  auto top_res = residual(top_x, top_rhs, par_x0); // residual on current level
  top_res = res_fixup(top_res); // update residual using fine residual and C-F stencil

  if(rhs->size() == 1) {
    auto u = mg_single_level(mg_type, top_res, nullptr); // compute residual correction
    top_x = slab_op({top_x, u}, -1, "amr_mg:update", 1.0); // apply correction: x += u
  }
  else {
    auto u = relax(NU, top_res);                         // compute residual correction
    top_x = slab_op({top_x, u}, -1, "amr_mg:update", 1.0); // apply correction: x += u
    top_res = residual(u, top_res);                      // update residual: top_res -= A*u
    
    tail_x = amr_mg_cycle(
      mg_type,
      tail_rhs,
      tail_x,
      bottom_par_x,
      /*res_fixup*/
      [=](Ex<Slab> par_res) {
        // use restrict_reflux as proxy for data dependencies
        return slab_restrict_reflux(par_res, top_res, "residual-fixup", perf::restr);
      }
    );
    
    auto par_x1 = tail_x->head();                          // new coarse parent
    auto par_uc = slab_op({par_x1, par_x0}, -1,
                          "amr_mg:diff", 1.0);             // cumulative correction: par_x1-par_x0
    top_x = prolong_update(top_x, par_uc);                 // prolong correction
    
    top_res = residual(top_x, top_rhs, par_x1);            // update residual: res = rhs - A*x
    u = relax(NU, top_res);                                // compute residual correction
    top_x = slab_op({top_x, u}, -1, "amr_mg:update", 1.0); // apply correction: x += u
  }

  return cons(top_x, tail_x);
}

IList<Ex<Slab>> mg_solve(
    MGType mg_type,
    IList<Ex<Slab>> rhs,
    Ex<Slab> bottom_x
  ) {
  auto zero = rhs->map<Ex<Slab>>(slab_zero_like);

//  auto max_box_size = find_max_box_size(rhs);
  
  return converge_list(
    zero,
    rhs,
    [=] (IList<Ex<Slab>> xs) {
      return xs->map_ix<Ex<Slab>>(
        [=] (int ix, Ex<Slab> x) {
          return residual(x, (*rhs)[ix]);
        }
      );
    },
    [=](IList<Ex<Slab>> xs) {
      return amr_mg_cycle(mg_type, rhs, xs, bottom_x);
    },
    solve_iter_n
  );
}

} // namespace multigrid

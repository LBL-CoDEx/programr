#include "slab.hxx"

#include <unordered_map>
#include <string>
#include "boxtree.hxx"
#include "lowlevel/spookyhash.hxx"

using namespace programr;
using namespace programr::amr;
using namespace std;

namespace {
  Dependency make_dependency_cells(
      uint64_t data_id,
      const Box &box,
      int unit_per_cell_log2,
      uint64_t src_task,
      size_t elmt_sz
    ) {
    
    SpookyHasher h;
    h.consume("cells");
    h.consume(data_id);
    h.consume(box);
    
    Dependency d;
    d.src_task = src_task;
    d.digest = h.digest();
    d.size = elmt_sz * (box.elmt_n()>>(3*unit_per_cell_log2));
    
    return d;
  }
  const bool flag_counter = false;
  std::unordered_map<std::string, std::size_t> counter;
}


////////////////////////////////////////////////////////////////////////
// Slab

void Slab::datas(std::vector<Data*> &add_to) const {
  add_to.push_back(data);
}

void Slab::tasks(std::vector<std::uint64_t> &add_to) const {
  task_map->for_val([&](std::uint64_t t) {
    add_to.push_back(t);
  });
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Literal

void Expr_Slab_Literal::subexs(std::vector<Expr*> &add_to) const {
}

void Expr_Slab_Literal::show(std::ostream &o) const {
  o << "Expr_Slab_Literal";
}

void Expr_Slab_Literal::execute(Expr::ExecCxt &cxt) {
  Slab *res = (Slab*)this->result;
  res->task_map = BoxMap<uint64_t>::make_by_ix(
    res->level.boxes,
    [&](int ix) {
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, ix),
        /*data*/res->data->id,
        /*deps*/{},
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(ix),
        /*seconds*/0.0
      );
    }
  );
  
  // prune...
  // (nothing)

  if (::flag_counter) { ++::counter["literal"]; }
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Migrate

void Expr_Slab_Migrate::subexs(std::vector<Expr*> &add_to) const {
  add_to.push_back(x);
}

void Expr_Slab_Migrate::show(std::ostream &o) const {
  o << "Expr_Slab_Migrate("<<x<<")";
}

void Expr_Slab_Migrate::execute(Expr::ExecCxt &cxt) {
  Slab *x = this->x.result();

  Slab *res = new Slab;
  this->result = res;
  
  res->bdry = x->bdry;
  res->level = x->level;
  res->halo_n = x->halo_n;
  res->data = new Data;
  res->rank_map = new_ranks;
  res->elmt_sz = x->elmt_sz;
  
  res->task_map = BoxMap<uint64_t>::make_by_ix_box(
    res->level.boxes,
    [&](int ix, Box box) {
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, ix),
        /*data*/res->data->id,
        /*deps*/{
          make_dependency_cells(
            /*data_id*/x->data->id,
            /*box*/box,
            /*unit_per_cell_log2*/x->level.unit_per_cell_log2(),
            /*src_task*/(*x->task_map)(res->level.boxes, ix),
            /*elmt_sz*/res->elmt_sz
          )
        },
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(ix),
        /*seconds*/0.0
      );
    }
  );
  
  // prune...
  this->x = nullptr;

  if (::flag_counter) { ++::counter["migrate"]; }
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Op

void Expr_Slab_Op::subexs(std::vector<Expr*> &add_to) const {
  for(int i=0; i < (int)args.size(); i++)
    add_to.push_back(args[i]);
}

void Expr_Slab_Op::show(std::ostream &o) const {
  o << "Expr_Slab_Op(";
  for(int i=0; i < (int)args.size(); i++) {
    if(i > 0) o << ',';
    o << args[i];
  }
  o << ')';
}

void Expr_Slab_Op::execute(Expr::ExecCxt &cxt) {
  Slab *res = new Slab;
  this->result = res;
  
  res->data = new Data;
  
  if(~elmt_sz != 0)
    res->elmt_sz = elmt_sz;
  else
    res->elmt_sz = args[0].result()->elmt_sz;
  
  for(int i=0; i < (int)args.size(); i++) {
    Slab *arg = args[i].result();
    if(i == 0) {
      res->bdry = arg->bdry;
      res->level = arg->level;
      res->halo_n = arg->halo_n;
      res->rank_map = arg->rank_map;
    }
    else {
      DEV_ASSERT(res->bdry == arg->bdry);
      DEV_ASSERT(res->level == arg->level);
      DEV_ASSERT(res->halo_n == arg->halo_n);
    }
  }
  
  res->task_map = BoxMap<uint64_t>::make_by_ix_box(
    res->level.boxes,
    [&](int ix, Box box) {
      vector<Dependency> deps;
      int arg_n = args.size();
      
      for(int a=0; a < arg_n; a++) {
        Slab *arg = args[a].result();
        deps.push_back(
          make_dependency_cells(
            /*data_id*/arg->data->id,
            /*box*/box,
            /*unit_per_cell_log2*/res->level.unit_per_cell_log2(),
            /*src_task*/(*arg->task_map)(res->level.boxes, ix),
            /*elmt_sz*/res->elmt_sz
          )
        );
      }
      
      Pt<int> box_sz = box.size();
      
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, ix),
        /*data*/res->data->id,
        /*deps*/deps,
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(ix),
        /*seconds*/this->perf_wflops == 0.0 ? 0.0 : perf::compute_s(
          perf::Stencil3DParams(
            this->perf_wflops,
            /*ro*/{(double)arg_n,(double)arg_n,(double)arg_n,(double)arg_n},
            /*wo*/{1,1,1,1},
            /*rw*/{0,0,0,0}
          ),
          {box_sz[0], box_sz[1], box_sz[2]}
        )
      );
    }
  );
  
  // prune...
  for(int i=0; i < (int)this->args.size(); i++)
    this->args[i] = nullptr;

  if (::flag_counter) { ++::counter["op"]; }
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Halo

void Expr_Slab_Halo::subexs(std::vector<Expr*> &add_to) const {
  add_to.push_back(kid);
  if(par) add_to.push_back(par);
}

void Expr_Slab_Halo::show(std::ostream &o) const {
  o << "Expr_Slab_Halo(kid="<<kid<<",par="<<par<<")";
}

void Expr_Slab_Halo::execute(Expr::ExecCxt &cxt) {
  Slab *res = new Slab;
  this->result = res;
  
  Slab *kid = this->kid.result();
  Slab *par = this->par ? this->par.result() : nullptr;
  
  DEV_ASSERT(kid->halo_n == 0);
  DEV_ASSERT(!par || kid->bdry == par->bdry);
  DEV_ASSERT(!par || kid->elmt_sz == par->elmt_sz);
  
  res->bdry = kid->bdry;
  res->level = kid->level;
  res->halo_n = halo_n;
  res->data = new Data;
  res->rank_map = kid->rank_map;
  res->elmt_sz = kid->elmt_sz;
  
  res->task_map = BoxMap<uint64_t>::make_by_ix_box(
    res->level.boxes, // == kid->level.boxes
    [&](int ix, Box box) {
      vector<Dependency> task_deps;
      
      // self dependency on interior
      task_deps.push_back(
        make_dependency_cells(
          /*data_id*/kid->data->id,
          /*box*/box,
          /*unit_per_cell_log2*/kid->level.unit_per_cell_log2(),
          /*src_task*/(*kid->task_map)(kid->level.boxes, ix),
          /*elmt_sz*/res->elmt_sz
        )
      );
      
      auto halo_deps = boxtree::deps_halo(
        /*kids*/kid->level,
        /*pars*/par ? &par->level : nullptr,
        /*kid_ix*/ix,
        /*halo*/halo_n,
        /*bdry*/res->bdry,
        /*prolong_halo*/prolong_halo_n
      );
      
      for(auto halo_dep: halo_deps) {
        int dep_lev, dep_ix; Box dep_box;
        tie(dep_lev, dep_ix, dep_box) = halo_dep;
        
        Slab *dep_res = dep_lev == 0 ? kid : par;
        
        task_deps.push_back(
          make_dependency_cells(
            /*data_id*/dep_res->data->id,
            /*box*/dep_box,
            /*unit_per_cell_log2*/dep_res->level.unit_per_cell_log2(),
            /*src_task*/(*dep_res->task_map)(dep_res->level.boxes, dep_ix),
            /*elmt_sz*/res->elmt_sz
          )
        );
      }
      
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, ix),
        /*data*/res->data->id,
        /*deps*/task_deps,
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(ix),
        /*seconds*/0.0
      );
    }
  );

  // prune...
  this->kid = nullptr;
  this->par = nullptr;

  if (::flag_counter) { ++::counter["halo"]; }
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Halo2

void Expr_Slab_Halo2::subexs(std::vector<Expr*> &add_to) const {
  add_to.push_back(kid);
  if(par0) add_to.push_back(par0);
  if(par1) add_to.push_back(par1);
}

void Expr_Slab_Halo2::show(std::ostream &o) const {
  o << "Expr_Slab_Halo2(kid="<<kid<<",par0="<<par0<<",par1="<<par1<<")";
}

void Expr_Slab_Halo2::execute(Expr::ExecCxt &cxt) {
  Slab *kid = this->kid.result();
  Slab *par0 = this->par0 ? this->par0.result() : nullptr;
  Slab *par1 = this->par1 ? this->par1.result() : nullptr;
  
  DEV_ASSERT(kid->halo_n == 0);
  
  DEV_ASSERT((par0 == nullptr) == (par1 == nullptr));
  DEV_ASSERT(par0 == nullptr || par0->level == par1->level);
  
  DEV_ASSERT(par0 == nullptr || kid->bdry == par0->bdry);
  DEV_ASSERT(par0 == nullptr || kid->elmt_sz == par0->elmt_sz);
  
  DEV_ASSERT(par1 == nullptr || kid->bdry == par1->bdry);
  DEV_ASSERT(par1 == nullptr || kid->elmt_sz == par1->elmt_sz);
  
  Slab *res = new Slab;
  this->result = res;
  
  res->bdry = kid->bdry;
  res->level = kid->level;
  res->halo_n = halo_n;
  res->data = new Data;
  res->rank_map = kid->rank_map;
  res->elmt_sz = kid->elmt_sz;
  
  res->task_map = BoxMap<uint64_t>::make_by_ix_box(
    res->level.boxes, // == kid->level.boxes
    [&](int ix, Box box) {
      vector<Dependency> task_deps;
      
      // self dependency on interior
      task_deps.push_back(
        make_dependency_cells(
          /*data_id*/kid->data->id,
          /*box*/box,
          /*unit_per_cell_log2*/kid->level.unit_per_cell_log2(),
          /*src_task*/(*kid->task_map)(res->level.boxes, ix),
          /*elmt_sz*/res->elmt_sz
        )
      );
      
      auto halo_deps = boxtree::deps_halo(
        /*kids*/kid->level,
        /*pars*/par0 ? &par0->level : nullptr,
        /*kid_ix*/ix,
        /*halo*/halo_n,
        /*bdry*/res->bdry,
        /*prolong_halo*/prolong_halo_n
      );
      
      for(auto halo_dep: halo_deps) {
        int dep_lev, dep_ix; Box dep_box;
        tie(dep_lev, dep_ix, dep_box) = halo_dep;
        
        if(dep_lev == 0) { // dep is sibling
          task_deps.push_back(
            make_dependency_cells(
              /*data_id*/kid->data->id,
              /*box*/dep_box,
              /*unit_per_cell_log2*/kid->level.unit_per_cell_log2(),
              /*src_task*/(*kid->task_map)(kid->level.boxes, dep_ix),
              /*elmt_sz*/res->elmt_sz
            )
          );
        }
        else { // dep is a parent
          for(Slab *par: {par0, par1}) {
            task_deps.push_back(
              make_dependency_cells(
                /*data_id*/par->data->id,
                /*box*/dep_box,
                /*unit_per_cell_log2*/par->level.unit_per_cell_log2(),
                /*src_task*/(*par->task_map)(par->level.boxes, dep_ix),
                /*elmt_sz*/res->elmt_sz
              )
            );
          }
        }
      }
      
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, ix),
        /*data*/res->data->id,
        /*deps*/task_deps,
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(ix),
        /*seconds*/0.0
      );
    }
  );

  // prune...
  this->kid = nullptr;
  this->par0 = nullptr;
  this->par1 = nullptr;

  if (::flag_counter) { ++::counter["halo2"]; }
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Stencil

void Expr_Slab_Stencil::subexs(std::vector<Expr*> &add_to) const {
  add_to.push_back(x);
}

void Expr_Slab_Stencil::show(std::ostream &o) const {
  o << "Expr_Slab_Stencil(x="<<x<<",halo="<<halo_n<<')';
}

void Expr_Slab_Stencil::execute(Expr::ExecCxt &cxt) {
  Slab *x = this->x.result();
  
  Slab *res = new Slab;
  this->result = res;
  
  res->bdry = x->bdry;
  res->level = x->level;
  res->halo_n = x->halo_n - halo_n;
  res->data = new Data;
  res->rank_map = x->rank_map;
  res->elmt_sz = x->elmt_sz;
  
  res->task_map = BoxMap<uint64_t>::make_by_ix_box(
    res->level.boxes,
    [&](int ix, Box box) {
      Pt<int> box_sz = box.size();
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, ix),
        /*data*/res->data->id,
        /*deps*/{
          make_dependency_cells(
            /*data_id*/x->data->id,
            /*box*/box.inflated(x->halo_n << x->level.unit_per_cell_log2()),
            /*unit_per_cell_log2*/x->level.unit_per_cell_log2(),
            /*src_task*/(*x->task_map)(res->level.boxes, ix),
            /*elmt_sz*/x->elmt_sz
          )
        },
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(ix),
        /*seconds*/perf::compute_s(
          this->perf_stencil,
          {box_sz[0], box_sz[1], box_sz[2]}
        )
      );
    }
  );
  
  // prune...
  this->x = nullptr;

  if (::flag_counter) { ++::counter["stencil"]; }
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Restrict

void Expr_Slab_Restrict::subexs(std::vector<Expr*> &add_to) const {
  add_to.push_back(kid);
  add_to.push_back(par);
}

void Expr_Slab_Restrict::show(std::ostream &o) const {
  o << "Expr_Slab_Restrict(par="<<par<<",kid="<<kid<<",reflux="<<reflux<<")";
}

void Expr_Slab_Restrict::execute(Expr::ExecCxt &cxt) {
  Slab *par = this->par.result();
  Slab *kid = this->kid.result();
  
  Slab *res = new Slab;
  this->result = res;
  
  res->bdry = kid->bdry;
  res->level = par->level;
  res->halo_n = 0;
  res->data = new Data;
  res->rank_map = par->rank_map;
  res->elmt_sz = kid->elmt_sz;
  
  res->task_map = BoxMap<uint64_t>::make_by_ix_box(
    par->level.boxes,
    [&](int par_ix, Box par_box) {
      vector<Dependency> deps;
      
      // dependency on parent
      deps.push_back(
        make_dependency_cells(
          /*data_id*/par->data->id,
          /*box*/par_box,
          /*unit_per_cell_log2*/par->level.unit_per_cell_log2(),
          /*src_task*/(*par->task_map)(par->level.boxes, par_ix),
          /*elmt_sz*/par->elmt_sz
        )
      );
      
      // children
      for(auto rest_dep: boxtree::deps_restrict(
          kid->level,
          par->level, par_ix
        )) {
        int kid_ix; Box kid_box;
        tie(kid_ix, kid_box) = rest_dep;
        
        uint64_t data_id = kid->data->id;
        
        SpookyHasher h;
        h.consume(reflux ? "reflux" : "restrict");
        h.consume(data_id);
        h.consume(kid_box);
        
        Dependency d;
        d.src_task = (*kid->task_map)(kid->level.boxes, kid_ix);
        d.digest = h.digest();
        int scale_log2 =
          (kid->level.box_scale_log2 - kid->level.cell_scale_log2) +
          (kid->level.cell_scale_log2 - par->level.cell_scale_log2);
        d.size = res->elmt_sz*(
          (kid_box.elmt_n() >> 3*scale_log2) + // averaged cells
          (reflux ? kid_box.bdry_face_n() >> 2*scale_log2 : 0) // averaged faces
        );
        deps.push_back(d);
      }
      
      Pt<int> box_sz = par_box.size();
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, par_ix),
        /*data*/res->data->id,
        /*deps*/deps,
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(par_ix),
        /*seconds*/perf::compute_s(this->perf_stencil, {box_sz[0], box_sz[1], box_sz[2]})
      );
    }
  );
  
  // prune...
  this->par = nullptr;
  this->kid = nullptr;
  if (::flag_counter) { ++::counter["restrict"]; }
}


////////////////////////////////////////////////////////////////////////
// Expr_Slab_Prolong

void Expr_Slab_Prolong::subexs(std::vector<Expr*> &add_to) const {
  add_to.push_back(par);
  add_to.push_back(kid);
}

void Expr_Slab_Prolong::show(std::ostream &o) const {
  o << "Expr_Slab_Prolong(kid="<<kid<<",par="<<par<<")";
}

void Expr_Slab_Prolong::execute(Expr::ExecCxt &cxt) {
  Slab *kid = this->kid.result();
  Slab *par = this->par.result();
  
  Slab *res = new Slab;
  this->result = res;
  
  res->bdry = par->bdry;
  res->level = kid->level;
  res->halo_n = 0;
  res->data = new Data;
  res->rank_map = kid->rank_map;
  res->elmt_sz = par->elmt_sz;
  
  res->task_map = BoxMap<uint64_t>::make_by_ix_box(
    kid->level.boxes,
    [&](int kid_ix, Box kid_box) {
      vector<Dependency> deps;
      
      for(auto pro_dep: boxtree::deps_prolong(
          /*kids*/kid->level,
          /*pars*/par->level,
          /*kid_ix*/kid_ix,
          /*bdry*/res->bdry,
          /*interp_halo*/prolong_halo_n
        )) {
        int par_ix; Box par_box;
        tie(par_ix, par_box) = pro_dep;
        
        deps.push_back(
          make_dependency_cells(
            /*data_id*/par->data->id,
            /*box*/par_box,
            /*unit_per_cell_log2*/par->level.unit_per_cell_log2(),
            /*src_task*/(*par->task_map)(par->level.boxes, par_ix),
            /*elmt_sz*/res->elmt_sz
          )
        );
      }
      
      Pt<int> box_sz = kid_box.size();
      return cxt.task(
        /*rank*/(*res->rank_map)(res->level.boxes, kid_ix),
        /*data*/res->data->id,
        /*deps*/deps,
        /*note*/note + " lev="+to_string(res->level.cell_scale_log2)+" box="+to_string(kid_ix),
        /*seconds*/perf::compute_s(this->perf_stencil, {box_sz[0], box_sz[1], box_sz[2]})
      );
    }
  );
  
  // prune...
  this->kid = nullptr;
  this->par = nullptr;

  if (::flag_counter) { ++::counter["prolong"]; }
}

void programr::amr::print_slab_counters() {
  if (::flag_counter) {
  Say() << "Slab counters:";
  for (auto p : ::counter) {
    auto tag = p.first;
    auto count = p.second;
    Say() << "  " << tag << ": " << count;
  }
  }
}

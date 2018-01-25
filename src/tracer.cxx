#include "tracer.hxx"
#include "diagnostic.hxx"

#include <algorithm>
#include <functional>
#include <iostream>
#include <vector>

using namespace programr;
using namespace std;

namespace {
  template<class Node, class F1, class F2, class F3>
  void traverse_depth_first(
      Node root,
      const F1 &enter, // bool(Node) : test if a node should be entered
      const F2 &kids, // void(vector<Node>&, Node) : get children of node
      const F3 &leave // void(Node) : leaving an entered node
    ) {
    vector<Node> x_stack(64);
    x_stack.resize(0);
    x_stack.push_back(root);
    
    vector<int> n_stack(32);
    n_stack.resize(0);
    
  entrance: {
      Node x = x_stack.back();
      if(enter(x)) {
        int n0 = (int)x_stack.size();
        kids(x_stack, x);
        int n = (int)x_stack.size() - n0;
        n_stack.push_back(n);
        goto next_kid;
      }
      else {
        x_stack.pop_back();
        if(n_stack.empty())
          return;
        else
          goto next_kid;
      }
    }
  next_kid: {
      int n = n_stack.back();
      if(n != 0) {
        n_stack.back() = n - 1;
        goto entrance;
      }
      else {
        Node x = x_stack.back();
        x_stack.pop_back();
        n_stack.pop_back();
        
        leave(x);
        
        if(n_stack.empty())
          return;
        else
          goto next_kid;
      }
    }
  }
  
  struct ExecCxt: Expr::ExecCxt {
    static uint64_t task_id_counter;
    static uint64_t rdxn_id_counter;

    Tracer *tracer;
    vector<uint64_t> *rdxn_ids;
    bool computed = false;
    bool reduced = false;
    
    uint64_t task(int rank, uint64_t data_id, const vector<Dependency> &deps, std::string note, double seconds) {
      computed = true;
      
      vector<Tracer::TaskDepTask> dep_tasks;
      
      // build task-to-task dependencies by merging deps with same src_task
      for(int i=0; i < (int)deps.size(); i++) {
        for(int j=0; j < (int)dep_tasks.size(); j++) {
          if(deps[i].src_task == dep_tasks[j].task) {
            dep_tasks[j].bytes += deps[i].size;
            dep_tasks[j].digest ^= deps[i].digest;
            goto merged;
          }
        }
        // not merged
        dep_tasks.push_back({deps[i].src_task, deps[i].size, deps[i].digest});
      merged:;
      }
      
      uint64_t task_id = task_id_counter++;
      tracer->task(task_id, rank, data_id, dep_tasks, *rdxn_ids, std::move(note), seconds);
      
      return task_id;
    }
    
    void reduction(size_t bytes, const vector<uint64_t> &dep_tasks) {
      reduced = true;

      uint64_t rdxn_id = rdxn_id_counter++;
      tracer->reduction(rdxn_id, bytes, dep_tasks, *rdxn_ids);
      rdxn_ids->clear();
      rdxn_ids->push_back(rdxn_id);
    }
  };
  
  uint64_t ExecCxt::task_id_counter = 0;
  uint64_t ExecCxt::rdxn_id_counter = 0;
}

namespace {
  template<class T>
  void uniquify(vector<T> &v) {
    std::sort(v.begin(), v.end());
    auto last = std::unique(v.begin(), v.end());
    v.erase(last, v.end());
  }
}

void Tracer::run(Ref<Expr> root) {
  LinkedList<Expr> ready(&Expr::links);
  
  function<void(Data*)> data_retirer = [this](Data *d) {
    this->retire(d->id);
  };
  
  auto add_expr = [&](Expr *e, vector<uint64_t> &dep_rdxn_ids) {
    bool has_fresh = false;
    
    traverse_depth_first<Expr*>(e,
      // enter: test if we havent entered this expression before
      [&](Expr *x)->bool {
        bool enter = x->state == Expr::fresh;
        if(enter) {
          x->state = Expr::registered;
          has_fresh = true;
        }
        return enter;
      },
      // kids: subexpressions
      [&](vector<Expr*> &ans, Expr *x) {
        size_t n0 = ans.size();
        x->subexs(ans);
        size_t n1 = ans.size();
        
        int pred_n = 0, fresh_pred_n = 0;
        
        for(size_t i=n0; i < n1; i++) {
          if(ans[i]->state != Expr::executed) {
            Expr::Succs *succ = Expr::succ_cons(x, ans[i]->succs);
            ans[i]->succs = succ;
            pred_n += 1;
          }
          if(ans[i]->state == Expr::fresh) {
            fresh_pred_n += 1;
          }
        }
        
        x->pred_n = pred_n;
        
        if(fresh_pred_n == 0) {
           // append
          x->dep_rdxn_ids.insert(x->dep_rdxn_ids.end(), dep_rdxn_ids.begin(), dep_rdxn_ids.end());
        }
        
        if(pred_n == 0)
          ready.push_tail(x);
      },
      // leave: no-op
      [&](Expr *x) {}
    );
    
    if(has_fresh) // if any fresh exprs were found then we consumed the reduction dependencies
      dep_rdxn_ids.clear();
  };
  
  { // add root
    vector<uint64_t> rdxn_ids;
    add_expr(root, rdxn_ids);
  }
  
  while(Expr *x = ready.pop_head()) {
    if(x->state == Expr::continued) {
      // inherit continuers rdxn ids
      x->dep_rdxn_ids.insert(x->dep_rdxn_ids.end(), x->continuer->dep_rdxn_ids.begin(), x->continuer->dep_rdxn_ids.end());
      uniquify(x->dep_rdxn_ids);
      
      // re-executing continuation just copies result forward
      x->state = Expr::executed;
      x->result = x->continuer->result;
      x->continuer = nullptr; // prune
      
      // on to notify x succs...
    }
    else {
      DEV_ASSERT(x->state == Expr::registered);
      
      // execute x
      //cout << string(60, '-') << '\n' << x << "\n\n";
      
      { // make x inherit its subexs "unconsumed" reduction dependencies
        vector<Expr*> subs;
        x->subexs(subs);
        for(Expr *dep: subs)
          x->dep_rdxn_ids.insert(x->dep_rdxn_ids.end(), dep->dep_rdxn_ids.begin(), dep->dep_rdxn_ids.end());
        uniquify(x->dep_rdxn_ids);
      }
      
      {
        ExecCxt exec_cxt;
        exec_cxt.tracer = this;
        exec_cxt.rdxn_ids = &x->dep_rdxn_ids;
        x->execute(exec_cxt);
        
        // if tasks were emitted clear out reduction deps
        if(exec_cxt.computed) {
          x->dep_rdxn_ids.clear();
          // hook for after a compute expression is emitted
          post_compute_exec();
        }
      }
      
      if(!x->continuer) {
        DEV_ASSERT(x->result);
        
        x->state = Expr::executed;
        
        vector<Data*> datas;
        x->result->datas(datas);
        for(Data *d: datas)
          d->retirer = data_retirer;
        
        // on to notify x succs...
      }
      else { // x is being continued
        DEV_ASSERT(!x->result);
        
        x->state = Expr::continued;
        
        if(x->continuer->state == Expr::executed) {
          // re-executing continuation just copies result forward
          x->state = Expr::executed;
          x->result = x->continuer->result;
          x->continuer = nullptr; // prune
          
          // on to notify x succs...
        }
        else {
          Ref<Expr> x_continuer = x->continuer;
          
          { // put x as a successor to its continuer
            x_continuer->succs = Expr::succ_cons(x, x_continuer->succs);
            x->pred_n = 1;
          }
        
          if(false) { // eliminate chains of continuations
            Ref<Expr> y1 = x;
            Ref<Expr> y0 = x_continuer;
            
            do {
              Expr::Succs **pp = &y1->succs;
              while(*pp) {
                RefWeak<Expr> &y2 = (*pp)->head;
                Expr::Succs *p = *pp;
                Expr::Succs *p_next = p->tail;
                  
                if(y2.is_dead()) {
                  *pp = p_next; // remove from y1->succs
                  Expr::succ_free(p);
                }
                else {
                  if(y2->state == Expr::continued) {
                    Expr::Succs *p = *pp;
                    Expr::Succs *p_next = p->tail;
                    // move p from y1->succs to y0->succs
                    p->tail = y0->succs;
                    y0->succs = p;
                    *pp = p_next;
                    // y2 points over y1 to y0 (may destroy y1)
                    y2->continuer = y0;
                  }
                  else
                    pp = &p->tail;
                }
              }
              
              y1 = std::move(y0);
              y0 = y1->continuer;
            } while(y0);
          }
          
          add_expr(x_continuer, x->dep_rdxn_ids);
          continue; // skip notify x succs
        }
      }
    }
    
    { // notify x succs that x has executed
      Expr::Succs *p = x->succs;
      x->succs = nullptr;
      
      while(p) {
        Expr::Succs *p_next = p->tail;
        if(!p->head.is_dead()) {
          Expr *y = p->head;
          if(0 == --y->pred_n)
            ready.push_tail(y);
        }
        Expr::succ_free(p);
        p = p_next;
      }
    }
  }
}

    
void TracerStdout::task(
    uint64_t id, int rank, uint64_t data,
    const vector<TaskDepTask> &dep_tasks,
    const vector<uint64_t> &dep_rdxns,
    std::string note,
    double seconds
  ) {
  if(false) {
    cout << "task id="<<id<<" rank="<<rank<<" data="<<data<<'\n';

    for(auto tdt: dep_tasks) {
      cout << "  dep_task "
        "id="<<tdt.task<<" "
        "bytes="<<tdt.bytes<<" "
        "digest="<<tdt.digest<<"\n";
    }
    
    for(auto rdxn: dep_rdxns)
      cout << "  dep_rdxn id="<<rdxn<<'\n';
  }
}

void TracerStdout::reduction(uint64_t id, std::size_t bytes, const vector<uint64_t> &dep_tasks, const vector<uint64_t> &dep_rdxns) {
  if(false) {
    cout << "rdxn id="<<id<<" bytes="<<bytes<<'\n';
    
    for(auto rdxn: dep_tasks)
      cout << " dep_task id="<<rdxn<<'\n';
  }
}

void TracerStdout::retire(uint64_t data) {
  //cout <<"retire data="<<data<<'\n';
}

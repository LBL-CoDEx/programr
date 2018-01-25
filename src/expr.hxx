#ifndef _87e46ff0_d4ee_493c_98aa_f825cad96476
#define _87e46ff0_d4ee_493c_98aa_f825cad96476

# include "data.hxx"
# include "diagnostic.hxx"
# include "list.hxx"
# include "lowlevel/digest.hxx"
# include "lowlevel/ref.hxx"
# include "lowlevel/linkedlist.hxx"
# include "lowlevel/pool.hxx"

# include <cstdint>
# include <functional>
# include <iostream>
# include <string>
# include <vector>

namespace programr {
  struct SourceLocation {
    const char *file;
    int line;
    
    SourceLocation(const char *file="???", int line=-1):
      file(file),
      line(line) {
    }
  };
  
# define _SLOC_ (programr::SourceLocation(__FILE__, __LINE__))

  inline std::ostream& operator<<(std::ostream &o, const SourceLocation &sloc) {
    return o << sloc.file << '@' << sloc.line;
  }
  
  
  struct Dependency {
    std::uint64_t src_task;
    Digest<128> digest; // Hash of the contents. Messages with matching digests are always interchangeable.
    std::size_t size;
  };
  
  struct Result: Referent {
    // get all datas exposed in this result
    virtual void datas(std::vector<Data*> &add_to) const = 0;
  };

  struct Expr: Referent {
    SourceLocation sloc;
    
    Expr(SourceLocation sloc={}): sloc(sloc) {}
    ~Expr();
    
    struct ExecCxt {
      virtual std::uint64_t task(int rank, std::uint64_t data_id, const std::vector<Dependency> &deps, std::string note, double seconds) = 0;
      virtual void reduction(std::size_t bytes, const std::vector<std::uint64_t> &dep_tasks) = 0;
    };
    
    // set by execute() iff this expr does not get continued
    Ref<Result> result = nullptr;
    // set by execute() iff this expr does get continued
    Ref<Expr> continuer = nullptr;
    
    // put subexpression pointers on vector
    virtual void subexs(std::vector<Expr*> &add_to) const = 0;
    // print some representation of this expression to stream
    virtual void show(std::ostream &o) const = 0;
    // produce a result xor continuation, and prune references to subexpressions
    virtual void execute(ExecCxt &cxt) = 0;
    
    
    ////////////////////////////////////////////////////////////////////
    // managed by runtime
    
    enum {
      fresh, // never seen by runtime before
      registered, // seen by runtime
      continued, // executed, but pending on a continuation
      executed // fully executed
    } state = fresh;
    
    int pred_n; // predecessor (aka sub-expression) count
    Links<Expr> links;
    std::vector<std::uint64_t> dep_rdxn_ids;
    
    struct Succs {
      RefWeak<Expr> head;
      Succs *tail;
    } *succs = nullptr; // successor list
    
    inline static Succs* succ_cons(Expr *head, Succs *tail) {
      return ::new(ThePool<Succs>::alloc()) Succs{head, tail};
    }
    inline static void succ_free(Succs *succ) {
      ThePool<Succs>::dealloc(succ);
    }
  };
  
  // produce an Expr from a ready result (monad return)
  Ref<Expr> ex_result(Ref<Result> result);
  
  template<class Result>
  struct Ex: Ref<Expr> {
    Ex(Expr *e=nullptr): Ref<Expr>(e) {}
    Ex(const Ex<Result>&) noexcept = default;
    Ex<Result>& operator=(const Ex<Result>&) noexcept = default;
    Ex(Ex<Result>&&) noexcept = default;
    Ex<Result>& operator=(Ex<Result>&&) noexcept = default;
    
    Result* result() const {
      return static_cast<Result*>((*this)->result);
    }
  };


  // produce an Expr from a ready result (monad return)
  template<class Res>
  inline Ex<Res> ex_result(Ref<Res> result) {
    return Ex<Res>(ex_result(Ref<Result>(std::move(result))));
  }
  
  inline std::ostream& operator<<(std::ostream &o, const Expr *e) {
    if(e) {
      e->show(o);
      o << '@' << (void*)e;
    }
    else
      o << "null";
    return o;
  }
}
namespace std {
  template<class Result>
  struct hash<programr::Ex<Result>> {
    size_t operator()(const programr::Ex<Result> &x) const {
      return hash<programr::Ref<programr::Expr>>()(x);
    }
  };
}
namespace programr {  
  //////////////////////////////////////////////////////////////////////
  // ExList<Result>
  
  template<class Res>
  struct ExList: Result {
    Imm<List<Ref<Res>>> list;
    
    void datas(std::vector<Data*> &add_to) const {
      list->for_val([&](Res *k) {
        k->datas(add_to);
      });
    }
    
    void tasks(std::vector<std::uint64_t> &add_to) const {
      list->for_val([&](Res *k) {
        k->tasks(add_to);
      });
    }
  };
  
  template<class Res>
  struct Expr_List: Expr {
    Imm<List<Ex<Res>>> _list;
    
    Expr_List(SourceLocation sloc, Imm<List<Ex<Res>>> list):
      Expr(sloc),
      _list(std::move(list)) {
    }
    
    void subexs(std::vector<Expr*> &add_to) const {
      _list->for_val([&](const Ex<Res> &e) {
        add_to.push_back((Expr*)e);
      });
    }
    
    void show(std::ostream &o) const {
      o << "Expr_List(len="<<(_list ? (int)_list->size() : -1)<<")";
    }
    
    void execute(ExecCxt &cxt) {
      ExList<Res> *res = new ExList<Res>;
      res->list = _list->template map<Ref<Res>>(
        [&](const Ex<Res> &e) {
          return e.result();
        }
      );
      this->result = res;
      
      // prune...
      this->_list = nullptr;
    }
  };
  
  template<class Res>
  inline Ex<ExList<Res>> _ex_list(SourceLocation sloc, Imm<List<Ex<Res>>> list) {
    return new Expr_List<Res>(sloc, std::move(list));
  }
  
  struct _Expr_List_SlocProxy {
    SourceLocation sloc;
    template<class Res>
    Ex<ExList<Res>> operator()(Imm<List<Ex<Res>>> list) {
      return _ex_list(sloc, std::move(list));
    }
  };
  
# define ex_list (programr::_Expr_List_SlocProxy{_SLOC_}).operator()
  
  template<class Res>
  IList<Ex<Res>> ex_list_items(Ex<ExList<Res>> xs, int n);
  
  
  //////////////////////////////////////////////////////////////////////
  // Expr_Meta
  
  class MetaEnv {
    std::vector<Ref<Expr>> _exprs;
    std::vector<Ref<Referent>> _metas;
  public:
    MetaEnv(
        std::vector<Ref<Expr>> exprs,
        std::vector<Ref<Referent>> metas
      ):
      _exprs(std::move(exprs)),
      _metas(std::move(metas)) {
    }
    
    template<class Res>
    Ref<Res> operator[](const Ex<Res> &e) const {
      for(int i=0; i < (int)_exprs.size(); i++) {
        if(_exprs[i] == e)
          return Ref<Res>(_metas[i]);
      }
      return nullptr;
    }
  };
  
  template<class B>
  class Expr_Meta: public Expr {
    std::vector<Ref<Expr>> _deps;
    std::function<Ex<B>(const MetaEnv&)> _cont;
  public:
    Expr_Meta(
        SourceLocation sloc,
        std::vector<Ref<Expr>> deps,
        std::function<Ex<B>(const MetaEnv&)> cont
      ):
      Expr(sloc),
      _deps(std::move(deps)),
      _cont(std::move(cont)) {
    }
    void subexs(std::vector<Expr*> &add_to) const;
    void show(std::ostream &o) const;
    void execute(Expr::ExecCxt &cxt);
  };
  
  template<class B>
  inline Ex<B> _ex_meta(SourceLocation sloc, std::vector<Ref<Expr>> deps, std::function<Ex<B>(const MetaEnv&)> cont) {
    return new Expr_Meta<B>(sloc, std::move(deps), std::move(cont));
  }
  
  struct _Expr_Meta_SlocProxy {
    SourceLocation sloc;
    template<class B>
    Ex<B> operator()(std::vector<Ref<Expr>> deps, std::function<Ex<B>(const MetaEnv&)> cont) {
      return _ex_meta(sloc, std::move(deps), std::move(cont));
    }
  };
  
# define ex_meta (programr::_Expr_Meta_SlocProxy{_SLOC_}).operator()


  //////////////////////////////////////////////////////////////////////
  // Expr_Meta<B> implementations
  
  template<class B>
  void Expr_Meta<B>::subexs(std::vector<Expr*> &add_to) const {
    for(Expr *e: _deps)
      add_to.push_back(e);
  }
  
  template<class B>
  void Expr_Meta<B>::show(std::ostream &o) const {
    o << "Expr_Meta(...)";
  }
  
  template<class B>
  void Expr_Meta<B>::execute(Expr::ExecCxt &cxt) {
    std::vector<Ref<Referent>> metas(_deps.size());
    
    for(int i=0; i < (int)_deps.size(); i++)
      metas[i] = _deps[i]->result;
    
    this->continuer = _cont(MetaEnv(std::move(_deps), std::move(metas)));
    
    // prune...
    this->_deps.clear();
    this->_cont = {};
  }
  
  
  //////////////////////////////////////////////////////////////////////
  // Expr_List implementation
  
  template<class Res>
  inline IList<Ex<Res>> ex_list_items(Ex<ExList<Res>> xs, int n) {
    std::vector<Ex<Res>> ys(n);
    for(int i=0; i < n; i++) {
      ys[i] = ex_meta<Res>({xs}, [=](const MetaEnv &env) {
        return ex_result(env[xs]->list->operator[](i));
      });
    }
    return List<Ex<Res>>::make(ys);
  }
}

#endif

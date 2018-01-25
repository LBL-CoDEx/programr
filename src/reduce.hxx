#ifndef _a8d549e8_f621_4ee0_a563_9fb85782f253
#define _a8d549e8_f621_4ee0_a563_9fb85782f253

# include "expr.hxx"

# include <functional>

namespace programr {
  template<class A, class S, class B>
  struct Expr_Reduce: Expr {
    Ex<A> _a;
    S _answer;
    std::function<Ex<B>(S)> _cont;
    
    Expr_Reduce(SourceLocation sloc): Expr(sloc) {}
    
    void subexs(std::vector<Expr*> &add_to) const;
    void show(std::ostream &o) const;
    void execute(Expr::ExecCxt &cxt);
  };
  
  template<class S, class B, class A>
  Ex<B> _ex_reduce(SourceLocation sloc, Ex<A> a, const S &fake_answer, std::function<Ex<B>(S)> cont) {
    Expr_Reduce<A,S,B> *ans = new Expr_Reduce<A,S,B>(sloc);
    ans->_a = std::move(a);
    ans->_answer = std::move(fake_answer);
    ans->_cont = std::move(cont);
    return Ex<B>(ans);
  }
  
  struct _Reduce_SlocProxy {
    SourceLocation sloc;
    template<class S, class B, class A>
    Ex<B> operator()(Ex<A> a, const S &fake_answer, std::function<Ex<B>(S)> cont) {
      return _ex_reduce(sloc, a, fake_answer, std::move(cont));
    }
  };
  
# define ex_reduce programr::_Reduce_SlocProxy{_SLOC_}.operator()
  
  
  //////////////////////////////////////////////////////////////////////
  // Expr_Reduce<A,S,B> implementations
  
  template<class A, class S, class B>
  void Expr_Reduce<A,S,B>::subexs(std::vector<Expr*> &add_to) const {
    add_to.push_back(_a);
  }
  
  template<class A, class S, class B>
  void Expr_Reduce<A,S,B>::show(std::ostream &o) const {
    o << "Expr_Reduce(" << _a << ")";
  }
  
  template<class A, class S, class B>
  void Expr_Reduce<A,S,B>::execute(Expr::ExecCxt &cxt) {
    std::vector<std::uint64_t> tasks;
    _a.result()->tasks(tasks);
    
    cxt.reduction(sizeof(S), tasks);
    
    this->continuer = _cont(_answer);
    
    // prune...
    this->_a = nullptr;
    this->_answer = {};
    this->_cont = {};
  }
}
#endif

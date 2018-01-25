#include "expr.hxx"

using namespace programr;
using namespace std;

Expr::~Expr() {
  Succs *p = succs;
  while(p) {
    Succs *p_next = p->tail;
    DEV_ASSERT(p->head.is_dead());
    succ_free(p);
    p = p_next;
  }
}

namespace {
  struct Expr_Return: Expr {
    Expr_Return(Ref<Result> result) {
      this->result = std::move(result);
      this->state = executed;
    }
    
    void subexs(std::vector<Expr*> &add_to) const {}
    void show(std::ostream &o) const { o << "Expr_Return(...)"; }
    void execute(ExecCxt &cxt) {}
  };
}

Ref<Expr> programr::ex_result(Ref<Result> result) {
  return new Expr_Return(std::move(result));
}

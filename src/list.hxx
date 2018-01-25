#ifndef _3112a820_0998_4818_abd3_9eb88dc62919
#define _3112a820_0998_4818_abd3_9eb88dc62919

# include "lowlevel/ref.hxx"

# include <cstddef>
# include <utility>
# include <new>

namespace programr {
  // immutable singly-linked list
  template<class T>
  struct List: Referent {
  //private:
    typename std::aligned_storage<sizeof(T),alignof(T)>::type _head_mem;
    Imm<List<T>> _tail;
    std::size_t _n;
    std::size_t _hash;
    
    static std::size_t _hash_cons(const T &head, std::size_t tail) {
      const std::size_t gold = 8*sizeof(size_t)==32 ? 0x9e3779b9u : 0x9e3779b97f4a7c15u;
      std::size_t h = std::hash<T>()(head) + tail;
      h ^= h >> 17;
      h *= gold;
      return h;
    }
  
  //private:
    List(int): // nil constructor
      Referent(lifetime_static),
      _tail(nullptr),
      _n(0),
      _hash(0) {
    }
    static List<T> _nil;
  
  public:
    List(T head, Imm<List<T>> tail):
      _tail(std::move(tail)) {
      
      ::new(&_head_mem) T(std::move(head));
      _n = 1 + _tail->_n;
      _hash = _hash_cons(this->head(), _tail->_hash);
    }
    // bogus constructor for late-construction
    List(nullptr_t) {}
    
    ~List() {
      if(_n != 0)
        reinterpret_cast<T&>(_head_mem).~T();
    }
    
    static List<T>* nil() {
      return &_nil;
    }
    
    friend Imm<List<T>> cons(T head, Imm<List<T>> tail) {
      return new List(std::move(head), std::move(tail));
    }
    
    static List<T>* make(std::initializer_list<T> xs);
    template<class Those>
    static List<T>* make(const Those &those);
    
    inline std::size_t size() const { return _n; }
    
    inline const T& head() const { return reinterpret_cast<const T&>(_head_mem); }
    inline const Imm<List<T>>& tail() const { return _tail; }
    
    const T& operator[](std::size_t ix) const {
      const List<T> *p = this;
      DEV_ASSERT(p != &_nil);
      while(ix-- != 0) {
        p = p->_tail;
        DEV_ASSERT(p != &_nil);
      }
      return p->head();
    }
    
    template<class F>
    void for_val(const F &f_val) const {
      const List<T> *p = this;
      while(p != &_nil) {
        f_val(p->head());
        p = p->_tail;
      }
    }
    
    template<class F>
    void for_ix_val(const F &f_ix_val) const {
      const List<T> *p = this;
      std::size_t ix = 0;
      while(p != &_nil) {
        f_ix_val(ix++, p->head());
        p = p->_tail;
      }
    }
    
    template<class U, class F>
    Imm<List<U>> map_ix(const F &f_ix_val) const;
    
    template<class U, class F>
    Imm<List<U>> map(const F &f_val) const;
    
    Imm<List<T>> reverse() const;
    
    Imm<List<T>> sublist(std::size_t i0, std::size_t i1) const;
    
  //private:
    
    // reverses a list in place and returns new head node.
    // list precondition:
    //   - all nodes have _ref_n=0.
    //   - the nil pointed to by last node should not have its _ref_n account for inclusion in this list
    //   - _n set correctly for reversed case.
    // postcondition:
    //    - valid list!
    static List<T>* _reverse_hack(List<T> *p);
  };
  
  template<class T>
  using IList = Imm<List<T>>;
  
  template<class T>
  List<T> List<T>::_nil(666);
  
  template<class T>
  bool operator==(const List<T> &a, const List<T> &b) {
    if(a._n != b._n || a._hash != b._hash)
      return false;
    
    const List<T> *x = &a, *y = &b;
    
    while(x != y) {
      if(x->head() != y->head())
        return false;
      x = x->_tail;
      y = y->_tail;
    }
    
    return true;
  }
  
  template<class T>
  List<T>* List<T>::make(std::initializer_list<T> xs) {
    return List<T>::make<std::initializer_list<T>>(xs);
  }
  
  template<class T>
  template<class Those>
  List<T>* List<T>::make(const Those &those) {
    List<T> *p = &List<T>::_nil;
    std::size_t n = those.size();
    
    // consruct in reversed order, all _ref_n==0
    for(const T &x: those) {
      List<T> *p1 = new List<T>(nullptr);
      p1->_tail._obj = p;
      p1->_n = n--;
      ::new(&p1->_head_mem) T(x);
      
      p = p1;
    }
    
    return _reverse_hack(p);
  }
  
  template<class T>
  List<T>* List<T>::_reverse_hack(List<T> *p) {
    List<T> *p_prev = &List<T>::_nil;
    std::size_t h_prev = 0;
    
    while(p != &List<T>::_nil) {
      List<T> *p_next = p->_tail._obj;
      
      p->_tail._obj = p_prev;
      p_prev->_ref_n += 1;
      
      h_prev = _hash_cons(p->head(), h_prev);
      p->_hash = h_prev;
      
      p_prev = p;
      p = p_next;
    }
    
    return p_prev;
  }
  
  template<class T>
  template<class U, class F>
  Imm<List<U>> List<T>::map_ix(const F &f_ix_val) const {
    const List<T> *a = this;
    List<U> *b = &List<U>::_nil;
    std::size_t ix = 0;
    
    // consruct in reversed order, all _ref_n==0
    while(a != &List<T>::_nil) {
      List<U> *b1 = new List<U>(nullptr);
      b1->_tail._obj = b;
      b1->_n = a->_n;
      ::new(&b1->_head_mem) U(f_ix_val(ix++, a->head()));
      
      a = a->_tail._obj;
      b = b1;
    }
    
    return List<U>::_reverse_hack(b);
  }
  
  template<class T>
  template<class U, class F>
  Imm<List<U>> List<T>::map(const F &f_val) const {
    return this->template map_ix<U>([&](int ix, const T &y) { return f_val(y); });
  }
  
  template<class T>
  Imm<List<T>> List<T>::reverse() const {
    const List<T> *a = this;
    List<T> *b = &List<T>::_nil;
    
    while(a != &List<T>::_nil) {
      b = new List<T>(a->head(), b);
      a = a->_tail._obj;
    }
    
    return b;
  }
  
  template<class T>
  Imm<List<T>> List<T>::sublist(std::size_t i0, std::size_t i1) const {
    const List<T> *a = this;
    List<T> *b = &List<T>::_nil;
    
    std::size_t n_sub = a->_n - i1;
    std::size_t ix = 0;
    
    while(ix < i0) {
      a = a->_tail._obj;
      ix += 1;
    }
    
    // consruct in reversed order, all _ref_n==0
    while(ix++ < i1) {
      List<T> *b1 = new List<T>(nullptr);
      b1->_tail._obj = b;
      b1->_n = a->_n - n_sub;
      ::new(&b1->_head_mem) T(a->head());
      
      a = a->_tail._obj;
      b = b1;
    }
    
    return List<T>::_reverse_hack(b);
  }
}
namespace std {
  template<class T>
  struct hash<programr::List<T>> {
    size_t operator()(const programr::List<T> &x) const {
      return x._hash;
    }
  };
}
#endif

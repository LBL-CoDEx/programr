#ifndef _b87a0a77_bb3a_4211_b05b_bb39146aa89d
#define _b87a0a77_bb3a_4211_b05b_bb39146aa89d

# include "bitops.hxx"
# include "pile.hxx"
# include "ref.hxx"
# include "typeclass.hxx"
# include "weaktraits.hxx"

# include <array>
# include <memory>
# include <new>
# include <tuple>
# include <type_traits>
# include <utility>

namespace programr {
  template<class T>
  struct HashEq {
    template<class Key>
    static std::size_t hash(const Key &x) {
      return std::hash<Key>()(x);
    }
    template<class Key, class Resident>
    static bool equals(const Key &key, const Resident &resident) {
      return key == resident;
    }
  };
  
  template<class T, class Eq=HashEq<T>>
  class WeakSet {
    struct Node {
      Node *next;
      typename std::aligned_storage<sizeof(T),alignof(T)>::type mem;
      
      inline T* address() { return reinterpret_cast<T*>(&mem); }
    };
    
    Pile _pile;
    Node *_frees;
    Node **_bkts;
    int _hbits;
    std::size_t _n;
    
  public:
    WeakSet();
    WeakSet(WeakSet<T,Eq> &&that);
    ~WeakSet();
    
    template<class Key, class KeyEq=Eq>
    T* get(const Key &x);
    template<class Key, class KeyEq=Eq>
    const T* get(const Key &x) const;
    
    template<class Key, class F, class KeyEq=Eq>
    void visit(const Key &x, const F &vtor);
    
    template<class Key>
    void put(const Key &x) {
      this->template visit([&](void *addr, bool &exists) {
        if(!exists)
          ::new(addr) T(x);
        else
          *(T*)addr = x;
      });
    }
    
    template<class Key>
    bool put_is_new(const Key &x) {
      bool is_new = false;
      this->template visit([&](void *addr, bool &exists) {
        if(!exists) {
          is_new = true;
          ::new(addr) T(x);
        }
        else
          *(T*)addr = x;
      });
      return is_new;
    }
    
    /* broken -- need to test for deadness
    template<class F>
    void for_each(const F &f) const {
      for(std::size_t b=0; b < std::size_t(1)<<_hbits; b++) {
        Node *p = _bkts[b];
        while(p) {
          f(*const_cast<const T*>(p->address()));
          p = p->next;
        }
      }
    }
    
    template<class F>
    void for_each(const F &f) {
      for(std::size_t b=0; b < std::size_t(1)<<_hbits; b++) {
        Node *p = _bkts[b];
        while(p) {
          f(*p->address());
          p = p->next;
        }
      }
    }*/
    
  private:
    inline static std::size_t bucket_of(std::size_t h, int hbits) {
      const std::size_t gold = 8*sizeof(std::size_t)==32 ? 0x9e3779b9u : 0x9e3779b97f4a7c15u;
      h ^= h >> 4*sizeof(std::size_t);
      h *= gold;
      return hbits==0 ? 0 : h>>(8*sizeof(std::size_t)-hbits);
    }
    
    Node* node_alloc();
    void ensure_enough_buckets();
  };
  
  template<class T, class Eq>
  WeakSet<T,Eq>::WeakSet() {
    _frees = nullptr;
    _hbits = 0;
    _bkts = (Node**)std::malloc(sizeof(Node*)<<_hbits);
    _n = 0;
    for(std::size_t i=0; i < std::size_t(1)<<_hbits; i++)
      _bkts[i] = nullptr;
  }
  
  template<class T, class Eq>
  WeakSet<T,Eq>::WeakSet(WeakSet<T,Eq> &&that):
    _pile(std::move(that._pile)),
    _frees(that._frees),
    _bkts(that._bkts),
    _hbits(that._hbits),
    _n(that._n) {
    
    that._bkts = nullptr;
  }
  
  template<class T, class Eq>
  WeakSet<T,Eq>::~WeakSet() {
    if(_bkts) {
      const std::size_t one = 1;
      for(std::size_t i=0; i < one<<_hbits; i++) {
        Node *p = _bkts[i];
        while(p) {
          p->address()->~T();
          p = p->next;
        }
      }
      std::free(_bkts);
    }
  }
  
  
  template<class T, class Eq>
  template<class Key, class KeyEq>
  T* WeakSet<T,Eq>::get(const Key &key) {
    using namespace std;
    
    size_t h = KeyEq::hash(key);
    size_t b = bucket_of(h, _hbits);
    Node **pp = &_bkts[b];
    
    while(*pp) {
      Node *p = *pp;
      T *x = p->address();
      
      if(any_dead(*x)) {
        _n -= 1;
        *pp = p->next;
        x->~T();
        p->next = _frees;
        _frees = p;
      }
      else if(KeyEq::equals(key, *x))
        return x;
      else
        pp = &p->next;
    }
    
    return nullptr;
  }
  
  template<class T, class Eq>
  template<class Key, class KeyEq>
  const T* WeakSet<T,Eq>::get(const Key &key) const {
    return const_cast<WeakSet<T,Eq>*>(this)->get(key);
  }
  
  template<class T, class Eq>
  template<class Key, class F, class KeyEq>
  void WeakSet<T,Eq>::visit(const Key &key, const F &vtor) {
    using namespace std;
    
    size_t h = KeyEq::hash(key);
    size_t b = bucket_of(h, _hbits);
    Node **pp = &_bkts[b];
    
    while(*pp) {
      Node *p = *pp;
      T *x = p->address();
      
      if(any_dead(*x)) {
        *pp = p->next;
        _n -= 1;
        x->~T();
        p->next = _frees;
        _frees = p;
      }
      else if(KeyEq::equals(key, *x)) {
        bool exists = true;
        vtor(x, exists); // reentrant

        if(!exists) { // remove p
          // h and p are still valid but b and pp are not (reentrance).
          b = bucket_of(h, _hbits);
          pp = &_bkts[b];
          while(*pp != p)
            pp = &(*pp)->next;
          *pp = p->next;
          _n -= 1;
          x->~T();
          p->next = _frees;
          _frees = p;
        }
        return;
      }
      else
        pp = &p->next;
    }
    
    // not found
    bool exists = false;
    Node *p = node_alloc();
    
    vtor(p->address(), exists); // reentrant
    
    if(exists) {
      b = bucket_of(h, _hbits);
      p->next = _bkts[b];
      _bkts[b] = p;
      _n += 1;
      
      ensure_enough_buckets();
    }
    else {
      p->next = _frees;
      _frees = p;
    }
  }
  
  template<class T, class Eq>
  inline typename WeakSet<T,Eq>::Node*
  WeakSet<T,Eq>::node_alloc() {
    Node *p;
    if(_frees) {
      p = _frees;
      _frees = p->next;
    }
    else
      p = _pile.push<Node>(/*n*/1, /*deft_page_sz*/4*sizeof(Node));
    return p;
  }
  
  template<class T, class Eq>
  void WeakSet<T,Eq>::ensure_enough_buckets() {
    using namespace std;
    
    const size_t one = 1;
    
    // desired length of average bucket
    int len = _n < 256 ? 4 : 2 + bitlog2up(_n)/4;
    // new hash length
    int hbits1 = bitlog2up((_n+len-1)/len);
    
    if(_hbits != hbits1) {
      Node **bkts1 = (Node**)std::malloc(sizeof(Node*)<<hbits1);
      
      for(size_t b1=0; b1 < one<<hbits1; b1++)
        bkts1[b1] = nullptr;
      
      for(size_t b0=0; b0 < one<<_hbits; b0++) {
        Node *p = _bkts[b0];
        while(p) {
          Node *p_next = p->next;
          T *x = p->address();
          
          if(any_dead(*x)) {
            x->~T();
            p->next = _frees;
            _frees = p;
            _n -= 1;
          }
          else {
            size_t h = Eq::hash(*x);
            size_t b1 = bucket_of(h, hbits1);
            p->next = bkts1[b1];
            bkts1[b1] = p;
          }
          
          p = p_next;
        }
      }
      
      std::free(_bkts);
      _bkts = bkts1;
      _hbits = hbits1;
    }
  }
}

#endif

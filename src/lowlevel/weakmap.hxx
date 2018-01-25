#ifndef _37f9321a_ae19_45c7_9d30_28a530c891da
#define _37f9321a_ae19_45c7_9d30_28a530c891da

# include "weakset.hxx"

namespace programr {
  template<class K, class V, bool weakval>
  struct _WeakMap_Arrow {
    K key;
    V val;
  };
  
  template<class K, class V, bool weakval>
  struct AnyDead<_WeakMap_Arrow<K,V,weakval>> {
    static bool apply(const _WeakMap_Arrow<K,V,weakval> &x) {
      return any_dead(x.key) || (weakval && any_dead(x.val));
    }
  };
  
  template<class K, class V, bool weakval>
  struct _WeakMap_HashEq {
    typedef _WeakMap_Arrow<K,V,weakval> Arrow;
    
    template<class K1>
    static std::size_t hash(const K1 &x) {
      return std::hash<K1>()(x);
    }
    static std::size_t hash(const Arrow &xy) {
      return std::hash<K>()(xy.key);
    }
    template<class K1>
    static bool equals(const K1 &a, const Arrow &b) {
      return a == b.key;
    }
  };
  
  template<class K, class V, bool weakval=true>
  class WeakMap {
    typedef _WeakMap_Arrow<K,V,weakval> Arrow;
    WeakSet<Arrow, _WeakMap_HashEq<K,V,weakval>> _set;
  public:
    const V* get(const K &key) const {
      const Arrow *xy = _set.get(key);
      return xy ? &xy->val : nullptr;
    }
    V* get(const K &key) {
      Arrow *xy = _set.get(key);
      return xy ? &xy->val : nullptr;
    }
    
    template<class F>
    V& at(const K &key, const F &val_ctor = [](void *p) {::new(p) V;}) {
      V *ans = nullptr;
      _set.visit(key, [&](void *p, bool &exists) {
        if(!exists) {
          ::new(&((Arrow*)p)->key) K(key);
          val_ctor(&((Arrow*)p)->val);
          exists = true;
        }
        ans = &((Arrow*)p)->val;
      });
      return *ans;
    }

    const V& operator[](const K &key) const {
      return *this->get(key);
    }
    V& operator[](const K &key) {
      return this->at(key);
    }
    
    template<class F>
    void for_key_val(const F &f_key_val) const {
      _set.for_each([&](const Arrow &a) {
        f_key_val(a.key, a.val);
      });
    }
    
    template<class F>
    void for_key_val(const F &f_key_val) {
      _set.for_each([&](Arrow &a) {
        f_key_val(const_cast<const K&>(a.key), a.val);
      });
    }
  };
}

#endif

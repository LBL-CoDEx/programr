#ifndef _1755cadf_44c9_409f_92e0_7dd8c6825e65
#define _1755cadf_44c9_409f_92e0_7dd8c6825e65

# include "boxlist.hxx"
# include "lowlevel/digest.hxx"

# include <cstdint>
# include <new>
# include <utility>

namespace programr {
namespace amr {
  template<class T>
  struct BoxMap: public Referent {
  //private:
    const Imm<BoxList> _keys;
    const ImmBoxed<T[]> _vals;
    const std::size_t _hash;
  public:
    BoxMap(Imm<BoxList> keys, ImmBoxed<T[]> vals):
      _keys(std::move(keys)),
      _vals(std::move(vals)),
      _hash(SeqHash(_keys).consume(_vals).hash()) {
    }
    
    static Ref<BoxMap<T>> make_constant(Imm<BoxList> keys, const T &val) {
      std::size_t n = keys->size();
      return new BoxMap<T>(
        std::move(keys),
        Boxed<T[],true>::alloc(n,
          [&](int i, void *p) {
            ::new(p) T(val);
          }
        )
      );
    }
    
    template<class F>
    static Ref<BoxMap<T>> make_by_ix_box(Imm<BoxList> keys, const F &f_ix_box) {
      ImmBoxed<T[]> vals;
      vals.alloc(
        keys->size(),
        [&](int i, void *p) {
          ::new(p) T(f_ix_box(i, (*keys)[i]));
        }
      );
      
      return new BoxMap<T>(std::move(keys), std::move(vals));
    }
    
    template<class F>
    static Ref<BoxMap<T>> make_by_ix(Imm<BoxList> keys, const F &f_ix) {
      std::size_t n = keys->size();
      return new BoxMap<T>(
        std::move(keys),
        Boxed<T[],true>::alloc(n,
          [&](int i, void *p) {
            ::new(p) T(f_ix(i));
          }
        )
      );
    }
    
    const T& operator()(const Box &box) const {
      return _vals[_keys->ix_of(box)];
    }
    const T& operator()(const Imm<BoxList> &boxes, int ix) const {
      if(_keys == boxes)
        return _vals[ix];
      else
        return _vals[_keys->ix_of((*boxes)[ix])];
    }
    
    template<class F>
    void for_key_val(const F &f_box_val) const {
      for(std::size_t i=0, n=_keys->size(); i < n; i++)
        f_box_val((*_keys)[i], _vals[i]);
    }
    
    template<class F>
    void for_val(const F &f_val) const {
      for(std::size_t i=0, n=_keys->size(); i < n; i++)
        f_val(_vals[i]);
    }
  };
  
  template<class T>
  bool operator==(const BoxMap<T> &a, const BoxMap<T> &b) {
    return
      a._hash == b._hash &&
      a._keys == b._keys &&
      a._vals == b._vals;
  }
}}
namespace std {
  template<class T>
  struct hash<programr::amr::BoxMap<T>> {
    size_t operator()(const programr::amr::BoxMap<T> &x) const {
      return x._hash;
    }
  };
}
#endif

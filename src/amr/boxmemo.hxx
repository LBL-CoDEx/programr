#ifndef _d00efa84_bb98_44a7_82ed_2a700251a7cf
#define _d00efa84_bb98_44a7_82ed_2a700251a7cf

# include "boxlist.hxx"
# include "lowlevel/pile.hxx"
# include "lowlevel/weakmap.hxx"

# include <memory>
# include <new>
# include <tuple>
# include <utility>

namespace programr {
namespace amr {
  // Memoizes a funcition `fn` of the form
  //   Result fn(Imm<BoxList> boxes, int ix, Args ...args)
  //
  // Create with:
  //   auto memo = boxmemoize(fn);
  //
  // Call with:
  //   memo(boxes, ix, args...)
  //
  template<class Res, class ...Args>
  class BoxMemo {
    typedef typename Weaken<std::tuple<Imm<BoxList>,Args...>>::type Key;
    
    struct Vals {
      typedef typename std::aligned_storage<sizeof(Res),alignof(Res)>::type Blob;
      
      std::size_t n;
      std::unique_ptr<std::size_t[]> bits;
      std::unique_ptr<Blob[]> blobs;
      
      Vals(std::size_t n, std::size_t *bits, Blob *blobs):
        n(n),
        bits(bits),
        blobs(blobs) {
      }
      ~Vals() {
        const std::size_t word_bits = 8*sizeof(std::size_t);
        for(std::size_t i=0; i < n; i++) {
          if(1 & (bits[i/word_bits] >> i%word_bits))
            reinterpret_cast<Res&>(blobs[i]).~Res();
        }
      }
    };
    
    WeakMap<Key, Vals> _map;
    Res(*_fn)(Imm<BoxList> boxes, int ix, Args ...args);
    
  public:
    BoxMemo(Res(*fn)(Imm<BoxList> boxes, int ix, Args ...args)):
      _fn(fn) {
    }
    BoxMemo(BoxMemo<Res, Args...>&&) = default;
    
    Res& operator()(const Imm<BoxList> &boxes, int ix, const Args&...args);
  };

  
  template<class Res, class ...Args>
  BoxMemo<Res,Args...> boxmemoize(Res(*fn)(Imm<BoxList> boxes, int ix, Args ...args)) {
    return BoxMemo<Res,Args...>(fn);
  }
  
  template<class Res, class ...Args>
  Res& BoxMemo<Res,Args...>::operator()(
      const Imm<BoxList> &boxes,
      int ix,
      const Args &...args
    ) {
    
    const std::size_t word_bits = 8*sizeof(std::size_t);
    
    Vals &vals = _map.at(
      std::tuple<Imm<BoxList> const&, Args const&...>(boxes, args...),
      [&](void *p) {
        size_t n = boxes->size();
        size_t bitword_n = (n + word_bits-1)/word_bits;
        new(p) Vals(n,
          /*bits */new std::size_t[bitword_n](), // zeroed
          /*blobs*/new typename Vals::Blob[n]
        );
      }
    );
    
    if(0 == (1 & (vals.bits[ix/word_bits] >> (ix%word_bits)))) {
      vals.bits[ix/word_bits] |= std::size_t(1)<<(ix%word_bits);
      ::new(&vals.blobs[ix]) Res(_fn(boxes, ix, args...));
    }
    
    return reinterpret_cast<Res&>(vals.blobs[ix]);
  }
  
  
  // Memoizes a funcition `fn` of the form:
  //   std::uint8_t* fn(Imm<BoxList> boxes, int ix, Args ...args, std::function<std::uint8_t*(std::size_t)> alloc)
  //
  // where `fn` allocates the bytes it stores the result in using `alloc`.
  //
  // Create with:
  //   auto memo = boxmemoize_bytes(fn);
  //
  // Call with:
  //   memo(boxes, ix, args...)
  //
  template<class ...Args>
  class BoxMemoBytes {
    typedef typename Weaken<std::tuple<Imm<BoxList>,Args...>>::type Key;
    
    struct Vals {
      std::size_t n;
      std::unique_ptr<std::uint8_t*[]> ptrs;
      Pile pile;
      
      Vals(std::size_t n):
        n(n),
        ptrs(new std::uint8_t*[n]()) {
      }
    };
    
    WeakMap<Key, Vals> _map;
    std::uint8_t*(*_fn)(const std::function<std::uint8_t*(std::size_t)>&, Imm<BoxList>, int, Args...);
    
  public:
    BoxMemoBytes(std::uint8_t*(*fn)(const std::function<std::uint8_t*(std::size_t)>&, Imm<BoxList>, int, Args...)):
      _fn(fn) {
    }
    BoxMemoBytes(BoxMemoBytes<Args...>&&) = default;
    
    std::uint8_t* operator()(const Imm<BoxList> &boxes, int ix, const Args&...args);
  };

  
  template<class ...Args>
  BoxMemoBytes<Args...> boxmemoize_bytes(
      std::uint8_t*(&fn)(
        const std::function<std::uint8_t*(std::size_t)>&, Imm<BoxList>, int, Args...
      )
    ) {
    return BoxMemoBytes<Args...>(fn);
  }
  
  template<class ...Args>
  std::uint8_t* BoxMemoBytes<Args...>::operator()(
      const Imm<BoxList> &boxes,
      int ix,
      const Args &...args
    ) {
    
    Vals &vals = _map.at(
      std::tuple<Imm<BoxList> const&, Args const&...>(boxes, args...),
      [&](void *p) {
        ::new(p) Vals(boxes->size());
      }
    );
    
    if(vals.ptrs[ix] == nullptr) {
      vals.ptrs[ix] = _fn(
        [&](std::size_t sz) { return (std::uint8_t*)vals.pile.push(sz, 1); },
        boxes, ix, args...
      );
    }
    
    return vals.ptrs[ix];
  }
}}
#endif

#ifndef _b61286c5_ccd4_4f8b_b1fc_fd318d666f5e
#define _b61286c5_ccd4_4f8b_b1fc_fd318d666f5e

# include <array>
# include <utility>
# include <tuple>
# include <type_traits>

namespace programr {
  template<class T>
  bool operator!=(const T &a, const T &b) {
    return !(a == b);
  }
  
  inline std::size_t hash() {
    return 0;
  }
  template<class T, class ...Tail>
  inline std::size_t hash(const T &x0, const Tail &...xs) {
    std::size_t h = hash(xs...);
    h ^= h >> 13;
    h *= 41;
    h += std::hash<T>()(x0);
    return h;
  }
}

namespace programr {
  namespace _std {
    template<class Tup, int i, int n>
    struct hash_tuple {
      inline static std::size_t apply(const Tup &x) {
        std::size_t h = hash_tuple<Tup,i+1,n>::apply(x);
        h ^= h >> 13;
        h *= 41;
        h += std::hash<typename std::decay<typename std::tuple_element<i,Tup>::type>::type>()(std::get<i>(x));
        return h;
      }
    };
    template<class Tup, int n>
    struct hash_tuple<Tup,n,n> {
      inline static std::size_t apply(const Tup &x) {
        return 0;
      }
    };
  }
}

namespace std {
  template<class ...Ts>
  struct hash<tuple<Ts...>> {
    inline size_t operator()(const tuple<Ts...> &x) const {
      return programr::_std::hash_tuple<tuple<Ts...>,0,sizeof...(Ts)>::apply(x);
    }
  };
  template<class A, class B>
  struct hash<pair<A,B>> {
    inline size_t operator()(const pair<A,B> &x) const {
      size_t h = hash<A>()(x.first);
      h ^= h >> 13;
      h *= 41;
      h += hash<B>()(x.second);
      return h;
    }
  };
  template<class T, std::size_t n>
  struct hash<array<T,n>> {
    inline size_t operator()(const array<T,n> &x) const {
      size_t h = 0;
      for(std::size_t i=0; i < n; i++) {
        h ^= h >> 13;
        h *= 41;
        h += hash<T>()(x[i]);
      }
      return h;
    }
  };
}

#endif

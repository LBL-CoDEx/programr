#ifndef _e412a6af_20eb_48df_9206_0165632b3971
#define _e412a6af_20eb_48df_9206_0165632b3971

# include "ref.hxx"

# include <array>
# include <tuple>
# include <utility>

namespace programr {
  // Weaken<T>: specialized per T
  // - typedef Weaken<T>::type:
  //   Maps type T to a Ref->RefWeak replaced type.
  template<class T>
  struct Weaken;

  template<class T>
  struct Weaken<const T&> {
    typedef typename Weaken<T>::type type;
  };
  
  template<class ...Args>
  struct Weaken<std::tuple<Args...>> {
    typedef std::tuple<typename Weaken<Args>::type...> type;
  };
  template<class A, class B>
  struct Weaken<std::pair<A,B>> {
    typedef std::pair<typename Weaken<A>::type, typename Weaken<B>::type> type;
  };
  template<class T, int n>
  struct Weaken<std::array<T,n>> {
    typedef std::array<typename Weaken<T>::type, n> type;
  };
  
  template<class T>
  struct Weaken<T*>:
    std::conditional<
      std::is_base_of<Referent,T>::value,
      RefWeak<T>,
      T*
    >::type {
  };
  
  template<class T, bool immutable>
  struct Weaken<Ref<T,immutable>> {
    typedef RefWeak<T,immutable> type;
  };
  template<class T, bool immutable>
  struct Weaken<RefWeak<T,immutable>> {
    typedef RefWeak<T,immutable> type;
  };
  template<class T, bool immutable>
  struct Weaken<RefBoxed<T,immutable>> {
    typedef RefWeak<Boxed<T,immutable>,immutable> type;
  };
  
  template<class T>
  struct Weaken {
    typedef T type;
  };
  
  // AnyDead<T>: specialized per T
  // - bool AnyDead<T>::apply(const T &x):
  //   detects if any of the weak references in x are dead.
  
  template<class T>
  struct AnyDead;
  
  template<class T>
  inline bool any_dead(const T &x) {
    return AnyDead<T>::apply(x);
  }
  
  template<class Tup, int i, int n>
  struct AnyDead_tuple {
    inline static bool apply(const Tup &x) {
      return any_dead(std::get<i>(x)) || AnyDead_tuple<Tup,i+1,n>::apply(x);
    }
  };
  template<class Tup, int n>
  struct AnyDead_tuple<Tup,n,n> {
    inline static bool apply(const Tup &x) {
      return false;
    }
  };
  
  template<class ...Args>
  struct AnyDead<std::tuple<Args...>> {
    inline static bool apply(const std::tuple<Args...> &x) {
      return AnyDead_tuple<std::tuple<Args...>, 0, sizeof...(Args)>::apply(x);
    }
  };
  
  template<class A, class B>
  struct AnyDead<std::pair<A,B>> {
    inline static bool apply(const std::pair<A,B> &x) {
      return any_dead(x.first) || any_dead(x.second);
    }
  };
  
  template<class T, int n>
  struct AnyDead<std::array<T,n>> {
    inline static bool apply(const std::array<T,n> &x) {
      bool ans = false;
      for(int i=0; i < n; i++)
        ans = ans || any_dead(x[i]);
      return ans;
    }
  };
  
  template<class T>
  struct AnyDead<RefWeak<T>> {
    inline static bool apply(const RefWeak<T> &r) {
      return r.is_dead();
    }
  };
  
  template<class T>
  struct AnyDead {
    inline static bool apply(const T &x) {
      return false;
    }
  };
}

#endif

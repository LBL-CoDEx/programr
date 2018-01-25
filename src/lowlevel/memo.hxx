#ifndef _e44371d4_129e_4751_9595_495a58580ec7
#define _e44371d4_129e_4751_9595_495a58580ec7

# include "diagnostic.hxx"
# include "lowlevel/ref.hxx"
# include "lowlevel/weakmap.hxx"

# include <new>
# include <utility>
# include <functional>

namespace programr {
  template<class Ret, class ...Args>
  class Memo {
    typedef typename Weaken<std::tuple<Args...>>::type WeakArgTup;
    
    WeakMap<WeakArgTup,Ret> _map;
    std::function<Ret(const Args&...)> _fn;
    //int _hit_n = 0;
  public:
    Memo(std::function<Ret(const Args&...)> &&fn):
      _fn(std::move(fn)) {
    }
    Memo(const Memo&) = delete;
    Memo(Memo<Ret,Args...> &&that):
      _map(std::move(that._map)),
      _fn(std::move(that._fn)) {
    }
    ~Memo() {
      //std::cout << "MEMO HITS="<<_hit_n<<"\n";
    }
    
    Ret& operator()(const Args &...args);
  };
  
  template<class Ret, class ...Args>
  Memo<Ret,typename std::decay<Args>::type...> memoize(Ret(*f)(Args...)) {
    return Memo<Ret,typename std::decay<Args>::type...>(f);
  }
  
  template<class Ret, class ...Args>
  Ret& Memo<Ret,Args...>::operator()(Args const &...args) {
    //bool hit = true;
    Ret &ans = _map.at(
      // key
      std::tuple<Args const&...>(args...),
      // constructor (if doesn't exist)
      [&](void *p) {
        //hit = false;
        ::new(p) Ret(_fn(args...));
      }
    );
    //_hit_n += hit ? 1 : 0;
    return ans;
  }
}

#endif

#ifndef _adca429b_ad48_4165_a06b_5987963b2c35
#define _adca429b_ad48_4165_a06b_5987963b2c35

# include "diagnostic.hxx"
# include "typeclass.hxx"

# include "digest.hxx"

# include <cstdint>
# include <iostream>

namespace programr {
  enum Lifetime {
    lifetime_static,
    lifetime_dynamic
  };
  
  // Reference countable object must inherit from this
  struct Referent {
    unsigned _ref_n;
    unsigned _weak_n;
    
    Referent(Lifetime lifetime=lifetime_dynamic):
      _ref_n(lifetime == lifetime_static ? ~0u>>1 : 0),
      _weak_n(0) {
    }
    
    virtual ~Referent() = default;
    
    void inform_storage_static() {
      _ref_n = ~0u>>1;
    }
    
    void _decref();
    void _decweak();
    
    Referent(const Referent&) = delete;
    Referent& operator=(const Referent&) = delete;
    Referent(Referent&&) = delete;
    Referent& operator=(Referent&&) = delete;
  };
  
  inline void Referent::_decref() {
    _ref_n -= 1;
    if(0 == _ref_n) {
      if(0 == _weak_n)
        delete this;
      else {
        unsigned weak_n = _weak_n;
        this->~Referent();
        _weak_n = weak_n;
        _ref_n = ~0u;
      }
    }
  }
  
  inline void Referent::_decweak() {
    _weak_n -= 1;
    if(0 == _weak_n && ~0u == _ref_n)
      ::operator delete(this);
  }
  
  // Base class of all reference like classes
  template<class Self, class T, bool immutable>
  struct RefLike {};
  
  // The reference counted pointer class. Immutable means equality and
  // hashing acts on the value being pointed to, not its address, ohterwise
  // equality is just address equality.
  template<class T, bool immutable=false>
  struct Ref: RefLike<Ref<T,immutable>,T,immutable> {
    mutable T *_obj;
    
    inline Ref(T *obj=nullptr) noexcept:
      _obj(obj) {
      if(_obj)
        _obj->_ref_n++;
    }
    
    inline Ref(const Ref<T,immutable> &that) noexcept {
      _obj = that._obj;
      if(_obj) _obj->_ref_n++;
    }
    template<class T1, bool imm1>
    inline Ref(const Ref<T1,imm1> &that) noexcept {
      _obj = static_cast<T*>(that._obj);
      if(_obj) _obj->_ref_n++;
    }
    
    template<class T1, bool imm1>
    inline Ref<T,immutable>& operator=(const Ref<T1,imm1> &that) noexcept {
      T1 *that_obj = that._obj;
      if(that_obj)
        that_obj->_ref_n++;
      if(this->_obj)
        this->_obj->_decref();
      this->_obj = that_obj;
      return *this;
    }
    inline Ref<T,immutable>& operator=(const Ref<T,immutable> &that) noexcept {
      return this->operator= <T,immutable>(that);
    }
    
    inline Ref(Ref<T,immutable> &&that) noexcept {
      _obj = that._obj;
      that._obj = nullptr;
    }
    template<class T1, bool imm1>
    inline Ref(Ref<T1,imm1> &&that) noexcept {
      _obj = static_cast<T*>(that._obj);
      that._obj = nullptr;
    }
    
    inline Ref<T,immutable>& operator=(Ref<T,immutable> &&that) noexcept {
      T *tmp = this->_obj;
      this->_obj = that._obj;
      that._obj = tmp;
      return *this;
    }
    template<bool imm1>
    inline Ref<T,immutable>& operator=(Ref<T,imm1> &&that) noexcept {
      T *tmp = this->_obj;
      this->_obj = that._obj;
      that._obj = tmp;
      return *this;
    }
    template<class T1, bool imm1>
    inline Ref<T,immutable>& operator=(Ref<T1,imm1> &&that) noexcept {
      T1 *that_obj = that._obj;
      if(this->_obj != that_obj) {
        that._obj = nullptr;
        if(this->_obj)
          this->_obj->_decref();
        this->_obj = static_cast<T*>(that_obj);
      }
      return *this;
    }
    
    inline ~Ref() noexcept {
      if(_obj) _obj->_decref();
    }
    
    inline operator T*() const { return _obj; }
    template<class T1>
    inline operator T1*() const { return static_cast<T1*>(_obj); }
    
    inline T& operator*() const { return *_obj; }
    inline T* operator->() const { return _obj; }
  };
  
  template<class T, bool immutable>
  std::ostream& operator<<(std::ostream &o, const Ref<T,immutable> &x) {
    return o << (T*)x;
  }
  
  // Weak reference version of Ref. is_dead() tells if this used to point
  // to an object that is no longer available. Dead references and null references
  // are not the same and will compare unequal, but all dead references are the same
  // and will compare equal.
  template<class T, bool immutable=false>
  struct RefWeak: RefLike<RefWeak<T,immutable>,T,immutable> {
    mutable T *_obj;
    
    RefWeak<T,immutable>& _undummy() const {
      if(0x1 < reinterpret_cast<std::uintptr_t>(_obj) && ~0u == _obj->_ref_n) {
        if(0 == --_obj->_weak_n) {
          //Say() << "Weak delete";
          ::operator delete(_obj);
        }
        _obj = reinterpret_cast<T*>(std::uintptr_t(0x1));
      }
      return const_cast<RefWeak<T,immutable>&>(*this);
    }
    
    RefWeak(T *obj=nullptr) noexcept {
      _obj = obj;
      if(_obj) _obj->_weak_n++;
    }
    
    RefWeak(const RefWeak<T,immutable> &that) noexcept:
      _obj(that._undummy()._obj) {
      if(0x1 < reinterpret_cast<std::uintptr_t>(_obj))
        _obj->_weak_n++;
    }
    template<class T1, bool imm1>
    RefWeak(const RefWeak<T1,imm1> &that) noexcept:
      _obj(static_cast<T*>(that._undummy()._obj)) {
      if(0x1 < reinterpret_cast<std::uintptr_t>(_obj))
        _obj->_weak_n++;
    }
    
    template<class T1, bool imm1>
    RefWeak(const Ref<T1,imm1> &that) noexcept:
      _obj(static_cast<T*>(that._obj)) {
      if(_obj)
        _obj->_weak_n++;
    }
    
    RefWeak<T,immutable>& operator=(const RefWeak<T,immutable> &that) noexcept {
      that._undummy();
      T *that_obj = that._obj;
      if(0x1 < reinterpret_cast<std::uintptr_t>(that_obj))
        that_obj->_weak_n++;
      if(0x1 < reinterpret_cast<std::uintptr_t>(this->_obj))
        this->_obj->_decweak();
      this->_obj = that_obj;
      return *this;
    }
    template<class T1, bool imm1>
    RefWeak<T,immutable>& operator=(const Ref<T1,imm1> &that) noexcept {
      T1 *that_obj = that._obj;
      if(that_obj)
        that_obj->_weak_n++;
      if(0x1 < reinterpret_cast<std::uintptr_t>(this->_obj))
        this->_obj->_decweak();
      this->_obj = static_cast<T*>(that_obj);
      return *this;
    }
    
    inline RefWeak(RefWeak<T,immutable> &&that) noexcept {
      _obj = that._obj;
      that._obj = nullptr;
    }
    template<class T1, bool imm1>
    inline RefWeak(RefWeak<T1,imm1> &&that) noexcept {
      _obj = static_cast<T*>(that._obj);
      that._obj = nullptr;
    }
    
    inline RefWeak<T,immutable>& operator=(RefWeak<T,immutable> &&that) noexcept {
      T *tmp = this->_obj;
      this->_obj = that._obj;
      that._obj = tmp;
      return *this;
    }
    template<class T1, bool imm1>
    inline RefWeak<T,immutable>& operator=(RefWeak<T1,imm1> &&that) noexcept {
      T1 *that_obj = that._obj;
      if(this->_obj != that_obj) {
        that._obj = nullptr;
        if(0x1 < reinterpret_cast<std::uintptr_t>(this->_obj))
          this->_obj->_decweak();
        this->_obj = static_cast<T*>(that_obj);
      }
      return *this;
    }
    
    inline ~RefWeak() noexcept {
      if(0x1 < reinterpret_cast<std::uintptr_t>(this->_obj))
        this->_obj->_decweak();
    }
    
    inline bool is_dead() const {
      _undummy();
      return 0x1 == reinterpret_cast<std::uintptr_t>(_obj);
    }
    
    inline operator T*() const {
      _undummy();
      return 0x1 < reinterpret_cast<std::uintptr_t>(_obj) ? _obj : nullptr;
    }
    inline T* operator->() const {
      _undummy();
      return 0x1 < reinterpret_cast<std::uintptr_t>(_obj) ? _obj : nullptr;
    }
    inline T& operator*() const {
      _undummy();
      return *_obj;
    }
  };
  
  
  //////////////////////////////////////////////////////////////////////
  // Referent boxes allow values of T to be reference counted when T
  // does not inherit from Referent
  
  // Base class for boxed referents
  template<class T, bool immutable>
  struct BoxedBase;
  
  template<class T>
  struct BoxedBase<T,true>: Referent {
    std::size_t _hash;
    
    template<class F>
    BoxedBase(void *mem, const F &ctor) {
      ctor(mem);
      this->_hash = hash(*(const T*)mem);
    }
    
    template<class F>
    std::size_t _do_hash(const F &f) const {
      return _hash;
    }
  };
  template<class T>
  struct BoxedBase<T,false>: Referent {
    template<class F>
    BoxedBase(void *mem, const F &ctor) {
      ctor(mem);
    }
    
    template<class F>
    std::size_t _do_hash(const F &f) const {
      return f();
    }
  };
  
  template<class T>
  struct BoxedBase<T[],true>: Referent {
    std::size_t _hash;
    
    BoxedBase(int):
      Referent(lifetime_static) {
      this->_hash = SeqHash(0).hash();
    }
    template<class F>
    BoxedBase(std::size_t n, void *elmts, const F &elmt_ctor) {
      SeqHash h(n);
      for(std::size_t i=0; i < n; i++) {
        T *elmt = (T*)elmts + i;
        elmt_ctor(i, (void*)elmt);
        h.consume(*elmt);
      }
      this->_hash = h.hash();
    }

    template<class F>
    std::size_t _do_hash(const F &f) const {
      return _hash;
    }
  };
  template<class T>
  struct BoxedBase<T[],false>: Referent {
    BoxedBase(int):
      Referent(lifetime_static) {
    }
    template<class F>
    BoxedBase(std::size_t n, void *elmts, const F &elmt_ctor) {
      for(std::size_t i=0; i < n; i++) {
        T *elmt = (T*)elmts + i;
        elmt_ctor(i, (void*)elmt);
      }
    }

    template<class F>
    std::size_t _do_hash(const F &f) const {
      return f();
    }
  };
  
  template<class T>
  inline bool maybe_equals(const BoxedBase<T,true> &a, const BoxedBase<T,true> &b) {
    return a._hash == b._hash;
  }
  template<class T>
  inline bool maybe_equals(const BoxedBase<T,false> &a, const BoxedBase<T,false> &b) {
    return true;
  }
  
  
  //////////////////////////////////////////////////////////////////////
  // Boxed<T> declaration
  
  template<class T, bool immutable=false>
  struct Boxed:
    public BoxedBase<T,immutable> {
    
    typedef BoxedBase<T,immutable> Base;
    
    typename std::aligned_storage<sizeof(T),alignof(T)>::type _elmt_mem;
    
    template<class Ctor>
    Boxed(const Ctor &ctor):
      Base((void*)&_elmt_mem, ctor) {
    }
    ~Boxed() {
      reinterpret_cast<T&>(_elmt_mem).~T();
    }
    
    inline T const& value() const { return reinterpret_cast<T const&>(_elmt_mem); }
    inline T      & value()       { return reinterpret_cast<T      &>(_elmt_mem); }
  };
  
  template<class T, bool immutable>
  inline bool operator==(const Boxed<T,immutable> &a, const Boxed<T,immutable> &b) {
    return maybe_equals(a, b) && a.value() == b.value();
  }
}
namespace std {
  template<class T, bool immutable>
  struct hash<programr::Boxed<T,immutable>> {
    size_t operator()(const programr::Boxed<T,immutable> &x) {
      return x._do_hash([&]() {
        return hash<T>()(x.value());
      });
    }
  };
}
namespace programr {

  //////////////////////////////////////////////////////////////////////
  // Boxed<T[]> for arrays
  
  template<class T, bool immutable>
  struct Boxed<T[],immutable>:
    public BoxedBase<T[], immutable> {
    
    typedef BoxedBase<T[], immutable> Base;
    
    std::size_t _n;
  
  private:
    template<class Ctor>
    Boxed(std::size_t n, std::size_t elmt_off, const Ctor &elmt_ctor):
      Base(n, (char*)this + elmt_off, elmt_ctor),
      _n(n) {
    }
    Boxed(int): // _empt_inst constructor
      Base(666),
      _n(0) {
    }
    
    static Boxed<T[],immutable> _empty_inst;
    
  public:
    ~Boxed();
    
    static Boxed<T[],immutable>* empty() {
      return &_empty_inst;
    }
    
    template<class Ctor>
    static Boxed<T[],immutable>* alloc(
      std::size_t n,
      const Ctor &elmt_ctor=[](std::size_t i, void *x){ ::new(x) T; }
    );
    
    inline std::size_t size() const { return _n; }
    
    inline const T* begin() const {
      std::size_t off = (sizeof(*this) + alignof(T)-1) & -alignof(T);
      return reinterpret_cast<const T*>((char*)this + off);
    }
    inline typename std::conditional<immutable, const T*, T*>::type
    begin() {
      std::size_t off = (sizeof(*this) + alignof(T)-1) & -alignof(T);
      return reinterpret_cast<T*>((char*)this + off);
    }
    
    inline const T* end() const { return begin() + _n; }
    inline typename std::conditional<immutable, const T*, T*>::type
           end() { return begin() + _n; }
    
    inline const T& operator[](std::size_t i) const { return begin()[i]; }
    inline typename std::conditional<immutable, const T&, T&>::type
           operator[](std::size_t i) { return begin()[i]; }
  };
  
  template<class T, bool immutable>
  Boxed<T[],immutable> Boxed<T[],immutable>::_empty_inst(666);
  
  template<class T, bool immutable>
  Boxed<T[],immutable>::~Boxed() {
    T *a = const_cast<T*>(begin());
    for(std::size_t i=0; i < _n; i++)
      a[i].~T();
  }
  
  template<class T, bool immutable>
  template<class Ctor>
  Boxed<T[],immutable>* Boxed<T[],immutable>::alloc(
      std::size_t n,
      const Ctor &elmt_ctor
    ) {
    std::size_t off = sizeof(Boxed<T[],immutable>);
    off = (off + alignof(T)-1) & -alignof(T);
    
    return ::new(operator new(off + n*sizeof(T)))
      Boxed<T[],immutable>(n, off, elmt_ctor);
  }
  
  template<class T, bool immutable>
  bool operator==(const Boxed<T[],immutable> &a, const Boxed<T[],immutable> &b) {
    if(!maybe_equals(a, b))
      return false;
    if(a._n != b._n)
      return false;
    
    const T *these = a.begin();
    const T *those = b.begin();
    
    for(std::size_t i=0, n=a._n; i < n; i++)
      if(these[i] != those[i])
        return false;
    
    return true;
  }
}
namespace std {
  template<class T, bool immutable>
  struct hash<programr::Boxed<T[],immutable>> {
    size_t operator()(const programr::Boxed<T[],immutable> &x) {
      return x._do_hash([&]()->size_t {
        programr::SeqHash h(x._n);
        const T *elmts = x.begin();
        
        for(std::size_t i=0, n=x._n; i < n; i++)
          h.consume(elmts[i]);
        
        return h.hash();
      });
    }
  };
}
namespace programr {
  
  
  //////////////////////////////////////////////////////////////////////
  // RefBoxed<T> and RefBoxed<T[]> - holds references to boxed T's
  
  template<class T, bool immutable=false>
  struct RefBoxed: Ref<Boxed<T,immutable>,immutable> {
    typedef RefBoxed<T,immutable> Me;
    typedef Boxed<T,immutable> Box;
    typedef Ref<Box,immutable> Base;
    
    RefBoxed(Box *obj=nullptr) noexcept: Base(obj) {}
    RefBoxed(const Me &that) noexcept = default;
    Me& operator=(Me &that) noexcept = default;
    RefBoxed(Me &&that) noexcept = default;
    Me& operator=(Me &&that) noexcept = default;
    
    template<class Ctor>
    Me& alloc(const Ctor &ctor) {
      this->~Me();
      Box *obj = new Box;
      obj->construct(ctor);
      ::new(this) Me(obj);
      return *this;
    }
    Me& alloc() {
      return this->alloc([](void *x) { ::new(x) T; });
    }
    
    inline typename std::conditional<immutable, const T&, T&>::type
      operator*() const { return this->_obj->value(); }
    inline typename std::conditional<immutable, const T*, T*>::type
      operator->() const { return &this->_obj->value(); }
  };
  
  template<class T, bool immutable>
  struct RefBoxed<T[],immutable>: Ref<Boxed<T[],immutable>,immutable> {
    typedef RefBoxed<T[],immutable> Me;
    typedef Boxed<T[],immutable> Box;
    typedef Ref<Box,immutable> Base;
    
    RefBoxed(Box *obj=nullptr) noexcept: Base(obj) {}
    RefBoxed(const Me &that) noexcept = default;
    Me& operator=(Me &that) noexcept = default;
    RefBoxed(Me &&that) noexcept = default;
    Me& operator=(Me &&that) noexcept = default;
    
    template<class Ctor>
    Me& alloc(std::size_t n, const Ctor &elmt_ctor) {
      return this->operator=(Me(Box::alloc(n, elmt_ctor)));
    }
    Me& alloc(std::size_t n) {
      return this->alloc(n, [](std::size_t i, void *x) { ::new(x) T; });
    }
    
    inline std::size_t size() const { return this->_obj->size(); }
    inline std::size_t size_safe() const { return this->_obj ? this->_obj->size() : 0; }
    
    inline typename std::conditional<immutable, const T&, T&>::type
      operator[](std::size_t i) const { return this->_obj->begin()[i]; }
  };
  
  
  //////////////////////////////////////////////////////////////////////
  // aliases
  
  template<class T>
  using Imm = Ref<T,true>;
  
  template<class T>
  using ImmWeak = RefWeak<T,true>;
  
  template<class T>
  using ImmBoxed = RefBoxed<T,true>;
  
  template<class T>
  using RefBoxedWeak = RefWeak<Boxed<T,true>>;
  
  template<class T>
  using ImmBoxedWeak = RefWeak<Boxed<T,true>,true>;
  
  
  //////////////////////////////////////////////////////////////////////
  // RefLike (all reference classes) equality operators
  
  // mutable == and !=
  template<class Ref_A, class A, class Ref_B, class B>
  inline bool operator==(const RefLike<Ref_A,A,false> &a, const RefLike<Ref_B,B,false> &b) {
    return static_cast<const Ref_A&>(a)._obj == static_cast<const Ref_B&>(b)._obj;
  }
  template<class Ref_A, class A, class Ref_B, class B>
  inline bool operator!=(const RefLike<Ref_A,A,false> &a, const RefLike<Ref_B,B,false> &b) {
    return static_cast<const Ref_A&>(a)._obj != static_cast<const Ref_B&>(b)._obj;
  }
  
  // immutable == and !=
  template<class Ref1, class Ref2, class T>
  bool operator==(const RefLike<Ref1,T,true> &a_, const RefLike<Ref2,T,true> &b_) {
    const Ref1 &a = static_cast<const Ref1&>(a_);
    const Ref2 &b = static_cast<const Ref2&>(b_);
    
    if(a._obj == b._obj)
      return true;
    else if(
        reinterpret_cast<std::uintptr_t>(a._obj) > 0x1 &&
        reinterpret_cast<std::uintptr_t>(b._obj) > 0x1 &&
        *a._obj == *b._obj) {
      // equivalent instances, make both point to a winner and discard the other
      bool a_wins =
        a._obj->_ref_n > b._obj->_ref_n ||
        (a._obj->_ref_n == b._obj->_ref_n && reinterpret_cast<std::uintptr_t>(a._obj) < reinterpret_cast<std::uintptr_t>(b._obj));
      if(a_wins)
        const_cast<Ref2&>(b) = Ref2(a._obj);
      else
        const_cast<Ref1&>(a) = Ref1(b._obj);
      return true;
    }
    else
      return false;
  }
  template<class Ref_A, class A, class Ref_B, class B>
  inline bool operator==(const RefLike<Ref_A,A,true> &a_, const RefLike<Ref_B,B,true> &b_) {
    const Ref_A &a = static_cast<const Ref_A&>(a_);
    const Ref_B &b = static_cast<const Ref_B&>(b_);
    return a._obj == b._obj || (
      reinterpret_cast<std::uintptr_t>(a._obj) > 0x1 &&
      reinterpret_cast<std::uintptr_t>(b._obj) > 0x1 &&
      *a._obj == *b._obj
    );
  }
  template<class Ref_A, class A, class Ref_B, class B>
  inline bool operator!=(const RefLike<Ref_A,A,true> &a, const RefLike<Ref_B,B,true> &b) {
    return !(a == b);
  }
  
  // == and != against nullptr
  template<class Ref_A, class A, bool imm>
  inline bool operator==(const RefLike<Ref_A,A,imm> &a, std::nullptr_t b) {
    return static_cast<const Ref_A&>(a)._obj == b;
  }
  template<class Ref_A, class A, bool imm>
  inline bool operator!=(const RefLike<Ref_A,A,imm> &a, std::nullptr_t b) {
    return static_cast<const Ref_A&>(a)._obj != b;
  }
  
  template<class Ref_B, class B, bool imm>
  inline bool operator==(std::nullptr_t a, const RefLike<Ref_B,B,imm> &b) {
    return a == static_cast<const Ref_B&>(b)._obj;
  }
  template<class Ref_B, class B, bool imm>
  inline bool operator!=(std::nullptr_t a, const RefLike<Ref_B,B,imm> &b) {
    return a != static_cast<const Ref_B&>(b)._obj;
  }
}

namespace std {
  template<class T>
  struct hash<programr::Ref<T,/*immutable=*/false>> {
    inline size_t operator()(const programr::Ref<T,false> &x) const {
      return hash<T*>()(x._obj);
    }
  };
  template<class T>
  struct hash<programr::Ref<T,/*immutable=*/true>> {
    inline size_t operator()(const programr::Ref<T,true> &x) const {
      return x._obj ? hash<T>()(*x._obj) : 0;
    }
  };
  
  template<class T>
  struct hash<programr::RefBoxed<T,/*immutable=*/false>> {
    inline size_t operator()(const programr::RefBoxed<T,false> &x) const {
      return hash<programr::Boxed<T,false>*>()(x._obj);
    }
  };
  template<class T>
  struct hash<programr::RefBoxed<T,/*immutable=*/true>> {
    inline size_t operator()(const programr::RefBoxed<T,true> &x) const {
      return x._obj ? hash<programr::Boxed<T,true>>()(*x._obj) : 0;
    }
  };
  
  template<class T>
  struct hash<programr::RefWeak<T,/*immutable=*/false>> {
    inline size_t operator()(const programr::RefWeak<T,false> &x) const {
      return hash<T*>()(x._obj);
    }
  };
  template<class T>
  struct hash<programr::RefWeak<T,/*immutable=*/true>> {
    inline size_t operator()(const programr::RefWeak<T,true> &x) const {
      return reinterpret_cast<std::uintptr_t>(x._obj) > 0x1
        ? hash<T>()(*x._obj)
        : reinterpret_cast<std::uintptr_t>(x._obj);
    }
  };
}
#endif

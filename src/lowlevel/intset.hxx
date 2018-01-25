#ifndef _fe486e95_7e17_453b_b0b3_bf1ae1e9f6e8
#define _fe486e95_7e17_453b_b0b3_bf1ae1e9f6e8

# include "bitops.hxx"
# include "byteseq.hxx"

# include <algorithm>
# include <cstdint>
# include <memory>

namespace programr {
  template<int bit_n, class I, class U>
  class IntSet1 {
    static constexpr U gold = IntGoldenRatio<bit_n>::value;
    
    struct Slot {
      I hi;
      U lo;
    };
    
    Slot *_slots;
    int _hbit_n;
    int _slot_n;
  
  public:
    IntSet1();
    IntSet1(const IntSet1<bit_n,I,U> &that);
    IntSet1<bit_n,I,U>& operator=(const IntSet1<bit_n,I,U> &that);
    IntSet1(IntSet1<bit_n,I,U> &&that);
    IntSet1<bit_n,I,U>& operator=(IntSet1<bit_n,I,U> &&that);
    ~IntSet1();
    
    bool has(I x) const;
    
    // returns previous truth of x's membership
    bool put(I x);
    
    template<class F>
    void for_each(const F &f) const;
    
    IntSet1<bit_n,I,U>& operator|=(const IntSet1<bit_n,I,U> &that);
    
    // a byteseq where the position of the one-bits reflect the ints in this set.
    ByteSeqBuilder as_byteseq() const;
    
    template<class F>
    static void flat_for_each(const std::uint8_t *flat, const F &f);
    
  private:
    void grow_if_needed();
    void grow(int hbit_n1);
  };
  
  
  template<class I>
  struct IntSet: IntSet1<8*sizeof(I), I, typename Unsigned<I>::type> {};
  
  
  template<int bit_n, class I, class U>
  IntSet1<bit_n,I,U>::IntSet1() {
    _hbit_n = 2;
    _slot_n = 0;
    int cap = 1<<_hbit_n;
    _slots = new Slot[cap];
    for(int i=0; i < cap; i++)
      _slots[i].lo = 0;
  }
  
  template<int bit_n, class I, class U>
  IntSet1<bit_n,I,U>::IntSet1(const IntSet1<bit_n,I,U> &that) {
    this->_hbit_n = that._hbit_n;
    this->_slot_n = that._slot_n;
    
    int cap = 1<<_hbit_n;
    this->_slots = new Slot[cap];
    
    for(int i=0; i < cap; i++)
      this->_slots[i] = that._slots[i];
  }
  
  template<int bit_n, class I, class U>
  IntSet1<bit_n,I,U>& IntSet1<bit_n,I,U>::operator=(const IntSet1<bit_n,I,U> &that) {
    if(this != &that) {
      if(this->_slots)
        delete[] this->_slots;
      
      this->_hbit_n = that._hbit_n;
      this->_slot_n = that._slot_n;
      
      int cap = 1<<_hbit_n;
      this->_slots = new Slot[cap];
      
      for(int i=0; i < cap; i++)
        this->_slots[i] = that._slots[i];
    }
    return *this;
  }
  
  template<int bit_n, class I, class U>
  IntSet1<bit_n,I,U>::IntSet1(IntSet1<bit_n,I,U> &&that) {
    this->_hbit_n = that._hbit_n;
    this->_slot_n = that._slot_n;
    this->_slots = that._slots;
    that._slots = nullptr;
  }
  
  template<int bit_n, class I, class U>
  IntSet1<bit_n,I,U>& IntSet1<bit_n,I,U>::operator=(IntSet1<bit_n,I,U> &&that) {
    using std::swap;
    swap(this->_hbit_n, that._hbit_n);
    swap(this->_slot_n, that._slot_n);
    swap(this->_slots, that._slots);
    return *this;
  }

  template<int bit_n, class I, class U>
  IntSet1<bit_n,I,U>::~IntSet1() {
    if(_slots)
      delete[] _slots;
  }
  
  template<int bit_n, class I, class U>
  bool IntSet1<bit_n,I,U>::has(I x) const {
    I hi = x & ~I(bit_n-1);
    I lo = x & I(bit_n-1);
    
    U h = U(hi);
    h ^= h >> bit_n/2;
    h *= gold;
    h >>= bit_n - _hbit_n;
    
    U i = 0;
    
    while(_slots[h].lo != 0) {
      if(_slots[h].hi == hi)
        return 0 != (1 & (_slots[h].lo >> lo));
      i += 1;
      h += i;
      h &= (U(1)<<_hbit_n)-1;
    }
    
    return false;
  }
  
  template<int bit_n, class I, class U>
  bool IntSet1<bit_n,I,U>::put(I x) {
    I hi = x & ~I(bit_n-1);
    I lo = x & I(bit_n-1);
    
    U h = U(hi);
    h ^= h >> bit_n/2;
    h *= gold;
    h >>= bit_n - _hbit_n;
    
    U i = 0;
    
    while(_slots[h].lo != 0) {
      if(_slots[h].hi == hi) {
        U m0 = _slots[h].lo;
        U m1 = m0 | U(1)<<lo;
        _slots[h].lo = m1;
        return m0 == m1;
      }
      i += 1;
      h += i;
      h &= (U(1)<<_hbit_n)-1;
    }
    
    // new slot
    _slots[h].hi = hi;
    _slots[h].lo = U(1)<<lo;
    _slot_n += 1;
    grow_if_needed();
    
    return false;
  }
  
  template<int bit_n, class I, class U>
  template<class F>
  void IntSet1<bit_n,I,U>::for_each(const F &f) const {
    int cap = 1<<_hbit_n;
    Slot *slots = _slots;
    
    for(int i=0; i < cap; i++) {
      U lo = slots[i].lo;
      while(lo != 0) {
        int b = bitffs(lo) - 1;
        lo &= lo-1;
        f(slots[i].hi | b);
      }
    }
  }

  template<int bit_n, class I, class U>
  std::ostream &operator<<(std::ostream &out, const IntSet1<bit_n,I,U> &iset ) {
    bool first = true;
    out << "(";
    iset.for_each( [&](I x) {
      if (first) first = false;
      else out << ",";
      out << x;
    });
    out << ")";
    return out;
  }
  
  template<int bit_n, class I, class U>
  IntSet1<bit_n,I,U>& IntSet1<bit_n,I,U>::operator|=(const IntSet1<bit_n,I,U> &that) {
    int cap1 = 1<<that._hbit_n;
    Slot *slots1 = that._slots;
    
    for(int i=0; i < cap1; i++) {
      if(slots1[i].lo != 0) {
        I hi = slots1[i].hi;
        U lo = slots1[i].lo;
        
        U h = U(hi);
        h ^= h >> bit_n/2;
        h *= gold;
        h >>= bit_n - this->_hbit_n;
        
        U i = 0;
        while(true) {
          if(this->_slots[h].lo == 0) {
            this->_slots[h].hi = hi;
            this->_slots[h].lo = lo;
            this->_slot_n += 1;
            this->grow_if_needed();
            break;
          }
          if(this->_slots[h].hi == hi) {
            this->_slots[h].lo |= lo;
            break;
          }
          i += 1;
          h += i;
          h &= (U(1)<<this->_hbit_n)-1;
        }
      }
    }
    
    return *this;
  }
  
  template<int bit_n, class I, class U>
  ByteSeqBuilder IntSet1<bit_n,I,U>::as_byteseq() const {
    int cap = 1<<_hbit_n;
    const Slot *slots = _slots;
    
    // count words
    int wn = 0;
    for(int b=0; b < cap; b++)
      wn += slots[b].lo != 0 ? 1 : 0;
    
    // find word indices
    std::unique_ptr<int[]> ws(new int[wn]); {
      int ix = 0;
      for(int b=0; b < cap; b++) {
        if(slots[b].lo != 0)
          ws[ix++] = b;
      }
    }
    
    // sort word indices
    std::sort(
      ws.get(), ws.get() + wn,
      [slots](int a, int b) { return slots[a].hi < slots[b].hi; }
    );
    
    ByteSeqBuilder bseq;
    I hi_prev = 0;
    
    for(int i=0; i < wn; i++) {
      U lo = slots[ws[i]].lo;
      I hi = slots[ws[i]].hi;
      
      bseq.add_zeros((hi - hi_prev)/8);
      hi_prev = hi + bit_n;
      
      for(int j=0; j < (int)sizeof(U); j++) {
        bseq.add_byte(lo & 0xff);
        lo >>= 8;
      }
    }
    
    return bseq;
  }

  template<int bit_n, class I, class U>
  inline void IntSet1<bit_n,I,U>::grow_if_needed() {
    int thresh = 0x3<<(_hbit_n-2);
    if(_slot_n >= thresh)
      grow(_hbit_n+1);
  }
  
  template<int bit_n, class I, class U>
  void IntSet1<bit_n,I,U>::grow(int hbit_n1) {
    int cap0 = 1<<_hbit_n;
    int cap1 = 1<<hbit_n1;
    Slot *slots0 = _slots;
    Slot *slots1 = new Slot[cap1];
    
    for(int h1=0; h1 < cap1; h1++)
      slots1[h1].lo = 0;
    
    for(int h0=0; h0 < cap0; h0++) {
      if(slots0[h0].lo != 0) {
        U h1 = U(slots0[h0].hi);
        h1 ^= h1 >> bit_n/2;
        h1 *= gold;
        h1 >>= bit_n - hbit_n1;
        
        U i = 0;
        while(slots1[h1].lo != 0) {
          i += 1;
          h1 += i;
          h1 &= (U(1)<<hbit_n1)-1;
        }
        slots1[h1].lo = slots0[h0].lo;
        slots1[h1].hi = slots0[h0].hi;
      }
    }
    
    delete[] _slots;
    _slots = slots1;
    _hbit_n = hbit_n1;
  }
}
#endif

#ifndef _6fd98601_d25b_40a6_a21f_e44cb6c5b78d
#define _6fd98601_d25b_40a6_a21f_e44cb6c5b78d

# include <algorithm>
# include <cstdint>
# include <new>

namespace programr {
  class Pile {
    struct Page {
      Page *prev;
      std::size_t height; // height of first byte in page
      std::uintptr_t end; // address of end of page
    };
    static Page dummy;
    Page *_head;
    Page *_hold;
    std::uintptr_t _edge;
    
  public:
    Pile();
    Pile(const Pile&) = delete;
    Pile& operator=(const Pile&) = delete;
    Pile(Pile&&); // leaves argument in a valid empty state
    Pile& operator=(Pile&&); // leaves argument in a valid empty state
    ~Pile();
    
    std::size_t height() const {
      using namespace std;
      return _head->height + (_edge-reinterpret_cast<uintptr_t>(_head+1));
    }
    
    void* push(std::size_t size, std::size_t align, std::size_t deft_page_sz=128);
    
    template<class T>
    T* push(std::size_t n=1, std::size_t deft_page_sz=128) {
      T *x = static_cast<T*>(this->push(n*sizeof(T), alignof(T), deft_page_sz));
      for(std::size_t i=0; i < n; i++)
        ::new(x+i) T;
      return x;
    }
    
    void chop(std::size_t height);
  
  private:
    void* push_oob(std::size_t size, std::size_t align, std::size_t deft_page_sz);
  };
  
  inline Pile::Pile() {
    _head = &dummy;
    _hold = 0x0;
    _edge = reinterpret_cast<std::uintptr_t>(&dummy+1);
  }
  
  inline Pile::Pile(Pile &&that) {
    _head = that._head;
    _hold = that._hold;
    _edge = that._edge;
    that._head = &dummy;
    that._hold = 0x0;
    that._edge = reinterpret_cast<std::uintptr_t>(&dummy+1);
  }
  
  inline Pile& Pile::operator=(Pile &&that) {
    using std::swap;
    swap(this->_head, that._head);
    swap(this->_hold, that._hold);
    swap(this->_edge, that._edge);
    return *this;
  }
  
  inline void* Pile::push(std::size_t size, std::size_t align, std::size_t deft_page_sz) {
    using namespace std;
    uintptr_t p = _edge;
    p = (p + align-1) & -uintptr_t(align);
    if(p + size > _head->end)
      return push_oob(size, align, deft_page_sz);
    _edge = p + size;
    return (void*)p;
  }
}

#endif

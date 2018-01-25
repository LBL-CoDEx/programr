#ifndef _81a21ed8_58eb_48ac_9e02_3bb79eea5a26
#define _81a21ed8_58eb_48ac_9e02_3bb79eea5a26

# include "linkedlist.hxx"
# include "diagnostic.hxx"
# include "knobs.hxx"

# include <cstdint>
# include <cstdlib>
# include <vector>

/* Pool<size,align>'s are efficient, non-threadsafe, allocators of
 * fixed size chunks of memory.
 * 
 * ThePool<T>::alloc/dealloc are conveniences for allocating from a single
 * global Pool<sizeof(T),alignof(T)> instance.
 */
namespace programr {
  namespace _ {
    template<class Slot, std::size_t slot_n>
    struct Pool_Block;
    
    template<class Slot, std::size_t slot_n>
    struct Pool_Page {
      char *blocks; /*Pool_Block<Slot,slot_n>*/
      unsigned short block_n; // no. of blocks on this page
      unsigned short empty_n; // no. of empty blocks on this page
      Links<Pool_Page<Slot,slot_n> > links;
      // ... actual block storage ...
    };
    
    template<class Slot, std::size_t slot_n>
    struct Pool_Block {
      Links<Pool_Block<Slot,slot_n> > links;
      Pool_Page<Slot,slot_n> *page;
      std::size_t mask;
      Slot slots[slot_n];
    };
    
    constexpr std::size_t ceilpow2(std::size_t x) {
      return (x&-x) == x ? x : ceilpow2(x + (x&-x));
    }
    
    template<class Slot>
    struct Pool_block_sz {
      static const std::size_t value =
        ceilpow2(sizeof(Pool_Block<Slot,1>) + sizeof(Slot)*(sizeof(unsigned)*8/2-1));
    };
    
    template<class Slot, std::size_t lb=sizeof(unsigned)*8/2, std::size_t ub=8*sizeof(unsigned)+1>
    struct Pool_slot_n {
      static const std::size_t
        block_sz = Pool_block_sz<Slot>::value,
        mid = (lb+ub)/2,
        sz_mid = sizeof(Pool_Block<Slot,mid>),
        value = sz_mid <= block_sz
          ? Pool_slot_n<Slot,mid+1,ub>::value
          : Pool_slot_n<Slot,lb,mid>::value;
    };

    template<class Slot, std::size_t lb>
    struct Pool_slot_n<Slot,lb,lb> {
      static const std::size_t value = lb-1;
    };
  }
  
  template<std::size_t size, std::size_t align>
  class Pool {
# if KNOB_POOL_JUST_MALLOC
    std::vector<void*> all;
# else
    typedef typename std::aligned_storage<size, align>::type Slot;
    
    static const std::size_t block_sz = _::Pool_block_sz<Slot>::value;
    static const std::size_t slot_n = _::Pool_slot_n<Slot>::value;
    
    typedef typename _::Pool_Block<Slot,slot_n> Block;
    typedef typename _::Pool_Page<Slot,slot_n> Page;
    
    static const std::size_t max_block_n = 8;
    static const std::size_t mask_full = ~std::size_t(0)>>(8*sizeof(std::size_t)-slot_n);
    
    LinkedList<Page> pages;
    LinkedList<Block> blocks_free;
    std::size_t blocks_free_n;
# endif
  private:
    void dealloc_all();
    
  public:
    // if you need the ability to free all memory allocated by a pool, then
    // instantiate one of these attached to the pool. it gives access to
    // dealloc_all(), and will call dealloc_all() automatically in its
    // destructor.
    class Eraser {
      Pool &pool;
    public:
      Eraser(Pool &pool): pool(pool) {}
      ~Eraser() { pool.dealloc_all(); }
      void dealloc_all() { pool.dealloc_all(); }
    };
    
  public:
    Pool();
    Pool(const Pool&) = delete;
    Pool(Pool&&) = default;
    Pool& operator=(const Pool&) = delete;
    Pool& operator=(Pool&&) = default;
    
    void *alloc();
    void dealloc(void *p);
  };
  
  namespace _ {
    template<std::size_t size, std::size_t align>
    struct Pool_global {
      static Pool<size,align> pool;
      static typename Pool<size,align>::Eraser eraser;
    };
    
    template<std::size_t size, std::size_t align>
    Pool<size,align> Pool_global<size,align>::pool;
    
    template<std::size_t size, std::size_t align>
    typename Pool<size,align>::Eraser Pool_global<size,align>::eraser{Pool_global<size,align>::pool};
  }
  
  template<class T>
  struct ThePool {
    // we don't return a T* because we don't want the user thinking we've
    // constructed it. what they should do is ::new(ThePool<T>::alloc()) T(...)
    static void* alloc() {
      return _::Pool_global<sizeof(T),alignof(T)>::pool.alloc();
    }
    static void dealloc(T *x) {
      x->~T();
      _::Pool_global<sizeof(T),alignof(T)>::pool.dealloc((void*)x);
    }
  };
  
  //////////////////////////////////////////////////////////////////////

#if KNOB_POOL_JUST_MALLOC
  
  template<std::size_t size, std::size_t align>
  Pool<size,align>::Pool() {}
  
  template<std::size_t size, std::size_t align>
  void Pool<size,align>::dealloc_all() {
    for(void *p: all)
      std::free(p);
  }
  
  template<std::size_t size, std::size_t align>
  void* Pool<size,align>::alloc() {
    void *ans = std::malloc(size);
    all.push_back(ans);
    return ans;
  }
  
  template<std::size_t size, std::size_t align>
  void Pool<size,align>::dealloc(void *x) {
    for(int i=0; i < (int)all.size(); i++) {
      if(all[i] == x) {
        all.erase(all.begin() + i);
        break;
      }
    }
    std::free(x);
  }
  
# else // #if KNOB_POOL_JUST_MALLOC

  template<std::size_t size, std::size_t align>
  Pool<size,align>::Pool():
    pages(&Page::links),
    blocks_free(&Block::links),
    blocks_free_n(0) {
  }
  
  template<std::size_t size, std::size_t align>
  void Pool<size,align>::dealloc_all() {
    for(Page *pg = pages.head(); pg != 0x0;) {
      Page *next = pg->next;
      std::free((void*)pg);
      pg = next;
    }
    
    // safe to clear these lists even though their elements have been
    // std::free'd because internally clear() just sets the head pointer to null.
    pages.clear();
    blocks_free.clear();
    blocks_free_n = 0;
  }
  
  template<std::size_t size, std::size_t align>
  void* Pool<size,align>::alloc() {
    Block *b = blocks_free.head();
    if(b == 0x0) {
      std::size_t sz = sizeof(Page) + max_block_n*sizeof(Block);
      Page *pg = (Page*)std::malloc(sz);
      std::uintptr_t u_pg = reinterpret_cast<std::uintptr_t>(pg);
      std::uintptr_t u_b = u_pg + sizeof(Page);
      u_b = (u_b + block_sz-1) & -std::uintptr_t(block_sz);
      
      pg->blocks = reinterpret_cast<char*>(u_b);
      // solve for block_n in: u_b + block_n*block_sz = u_pg + sz
      pg->block_n = (u_pg + sz - u_b)/block_sz;
      pg->empty_n = pg->block_n;
      pages.push_head(pg);
      
      blocks_free_n += pg->block_n;
      for(int i=pg->block_n; i--;) {
        b = reinterpret_cast<Block*>(pg->blocks + i*block_sz);
        b->page = pg;
        b->mask = 0;
        blocks_free.push_head(b);
      }
      // b now has address of first block in page
    }
    
    std::size_t s;
    if(b->mask == 0) {
      s = 0;
      b->mask = 1;
      b->page->empty_n -= 1;
    }
    else {
      DEV_ASSERT(b->mask < mask_full);
      s = __builtin_ffs(~b->mask) - 1;
      b->mask |= b->mask + 1;
    }
    
    if(b->mask == mask_full) {
      blocks_free.remove(b);
      blocks_free_n -= 1;
    }
    
    return (void*)(b->slots + s);
  }
  
  template<std::size_t size, std::size_t align>
  void Pool<size,align>::dealloc(void *x) {
    std::uintptr_t u_x = reinterpret_cast<std::uintptr_t>(x);
    std::uintptr_t u_b = u_x & -std::uintptr_t(block_sz);
    Block *b = reinterpret_cast<Block*>(u_b);
    Page *pg = b->page;
    
    if(b->mask == mask_full) {
      blocks_free.push_head(b);
      blocks_free_n += 1;
    }
    
    { // clear mask bit
      std::ptrdiff_t s = (Slot*)x - b->slots;
      std::size_t m0 = b->mask;
      std::size_t m1 = m0 & ~(std::size_t(1)<<s);
      DEV_ASSERT(m0 != m1);
      b->mask = m1;
    }
    
    if(b->mask == 0) {
      pg->empty_n += 1;
      if(pg->empty_n == pg->block_n && blocks_free_n > pg->block_n) {
        blocks_free_n -= pg->block_n;
        for(int i=pg->block_n; i--;)
          blocks_free.remove(reinterpret_cast<Block*>(pg->blocks + i*block_sz));
        pages.remove(pg);
        std::free((void*)pg);
      }
    }
  }
# endif
}

#endif

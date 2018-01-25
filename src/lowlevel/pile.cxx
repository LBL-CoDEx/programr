#include "pile.hxx"

#include <algorithm>

using namespace programr;
using namespace std;

Pile::Page Pile::dummy = {nullptr, 0, reinterpret_cast<uintptr_t>(&dummy+1)};

Pile::~Pile() {
  for(Page *pg=_head, *pg1; pg != &dummy; pg = pg1) {
    pg1 = pg->prev;
    std::free((void*)pg);
  }
  if(_hold) std::free((void*)_hold);
}

void* Pile::push_oob(std::size_t size, std::size_t align, std::size_t deft_page_sz) {
  uintptr_t p = _edge;
  p = (p + align-1) & -uintptr_t(align);
  if(p + size > _head->end) {
    Page *pg;
    if(_hold) {
      pg = _hold;
      _hold = nullptr;
    }
    else {
      size_t head_sz = _head->end - reinterpret_cast<uintptr_t>(_head);
      size_t deft = std::max(deft_page_sz, head_sz + head_sz/4);
      size_t page_sz = size <= deft/2 ? deft : size + deft_page_sz;
      pg = (Page*)std::malloc(page_sz);
      pg->end = reinterpret_cast<uintptr_t>(pg) + page_sz;
    }
    pg->prev = _head;
    pg->height = _head->height + (_edge-reinterpret_cast<uintptr_t>(_head+1));
    _head = pg;
    p = reinterpret_cast<uintptr_t>(pg+1);
    p = (p + align-1) & -uintptr_t(align);
  }
  _edge = p + size;
  return (void*)p;
}

void Pile::chop(std::size_t hgt) {
  Page *pg = _head;
  while(hgt <= pg->height && pg != &dummy) {
    Page *pg1 = pg->prev;
    if(!_hold)
      _hold = pg;
    else
      std::free((void*)pg);
    pg = pg1;
  }
  _edge = hgt-pg->height + reinterpret_cast<uintptr_t>(pg+1);
  _head = pg;
}

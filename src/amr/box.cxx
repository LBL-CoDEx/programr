#include "box.hxx"

#include <algorithm>

using namespace programr;
using namespace programr::amr;
using namespace std;

void Box::intersect(deque<Box> &xs, const Box &y) {
  int n0 = (int)xs.size();
  for(int i=0; i < n0; i++) {
    Box x = xs.front();
    xs.pop_front();
    
    x = Box::intersection(x, y);
    if(!x.is_empty())
      xs.push_back(x);
  }
}

void Box::intersect(deque<Box> &ans, const deque<Box> &xs, const Box &y) {
  for(const Box &x: xs) {
    Box z = Box::intersection(x, y);
    if(!z.is_empty())
      ans.push_back(z);
  }
}

void Box::subtract(deque<Box> &push_on, const Box &a, const Box &b) {
  if(!a.intersects(b))
    push_on.push_back(a);
  else {
    for(int d=0; d < 3; d++) {
      Box z;
      for(int e=0; e < d; e++) {
        z.lo[e] = max(a.lo[e], b.lo[e]);
        z.hi[e] = min(a.hi[e], b.hi[e]);
      }
      for(int e=d+1; e < 3; e++) {
        z.lo[e] = a.lo[e];
        z.hi[e] = a.hi[e];
      }
      
      if(a.lo[d] < b.lo[d]) {
        z.lo[d] = a.lo[d];
        z.hi[d] = b.lo[d];
        push_on.push_back(z);
      }
      if(b.hi[d] < a.hi[d]) {
        z.lo[d] = b.hi[d];
        z.hi[d] = a.hi[d];
        push_on.push_back(z);
      }
    }
  }
}

void Box::subtract(deque<Box> &xs, const Box &y) {
  int xs_n0 = (int)xs.size();
  
  for(int i=0; i < xs_n0; i++) {
    Box x = xs.front();
    xs.pop_front();
    subtract(xs, x, y);
  }
}

void Box::unify(deque<Box> &xs, const Box &y) {
  int xs_n0 = (int)xs.size();
  
  // replace `xs` with `xs-y`
  for(int i=0; i < xs_n0; i++) {
    Box x = xs.front();
    xs.pop_front();
    
    if(x.subsumes(y)) { // important shortcut
      xs.push_back(x);
      return;
    }
    else
      subtract(xs, x, y);
  }
  
  xs.push_back(y);
}

void Box::split(deque<Box> &xs, const Box &y) {
  int xs_n0 = (int)xs.size();
  
  for(int i=0; i < xs_n0; i++) {
    Box x = xs.front();
    xs.pop_front();
    
    subtract(xs, x, y);
    
    Box xy = Box::intersection(x, y);
    if(!xy.is_empty())
      xs.push_back(xy);
  }
}

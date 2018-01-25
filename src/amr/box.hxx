#ifndef _68be8c4d_afcd_463a_ab00_0717ba3c64a4
#define _68be8c4d_afcd_463a_ab00_0717ba3c64a4

# include "pt.hxx"
# include "typeclass.hxx"

# include <array>
# include <cstdint>
# include <deque>
# include <iostream>
# include <vector>

namespace programr {
namespace amr {
  struct Box {
    Pt<int> lo, hi;
    
    static Box empty() {
      return Box{Pt<int>(0), Pt<int>(0)};
    }
    
    bool is_empty() const {
      return any_le(hi, lo);
    }
    
    Box canonical() const {
      return is_empty() ? empty() : *this;
    }
    
    Pt<int> size() const {
      return hi-lo;
    }
    int elmt_n() const {
      Pt<int> sz = hi-lo;
      return sz.x[0]*sz.x[1]*sz.x[2];
    }
    int bdry_face_n() const {
      Pt<int> sz = hi-lo;
      return  2 * sz.x[1] * sz.x[2] +
        sz.x[0] *       2 * sz.x[2] +
        sz.x[0] * sz.x[1] *       2;
    }
    
    friend bool operator==(const Box &a, const Box &b) {
      return a.lo == b.lo && a.hi == b.hi;
    }
    friend bool operator!=(const Box &a, const Box &b) {
      return !(a == b);
    }
    
    bool contains(const Pt<int> &x) const {
      return all_le(lo, x) && all_lt(x, hi);
    }
    bool subsumes(const Box &that) const {
      return
        all_le(this->lo, that.lo) &&
        all_le(that.hi, this->hi);
    }
    bool intersects(const Box &that) const {
      return
        all_lt(this->lo, that.hi) &&
        all_lt(that.lo, this->hi);
    }
    
    static Box intersection(const Box &a, const Box &b) {
      Pt<int> lo = max(a.lo, b.lo);
      Pt<int> hi = min(a.hi, b.hi);
      return Box{lo, hi};
    }
    static Box covering(const Box &a, const Box &b) {
      Pt<int> lo = min(a.lo, b.lo);
      Pt<int> hi = max(a.hi, b.hi);
      return Box{lo, hi};
    }
    
    Box inflated(int dx) const {
      return Box{lo-dx, hi+dx};
    }
    
    std::array<Box,6> inflated_faces(int dx) const {
      std::array<Box,6> ans;
      for (int d = 0, idx = 0; d < 3; ++d) {
        Pt<int> alo{lo}, ahi{hi};
        // lower face
        alo[d] = lo[d]-dx; ahi[d] = lo[d];
        ans[idx++] = Box{alo, ahi};
        // upper face
        alo[d] = hi[d]; ahi[d] = hi[d]+dx;
        ans[idx++] = Box{alo, ahi};
      }
      return ans;
    }
    
    Box scaled_pow2(int factor_log2) const {
      if(factor_log2 > 0)
        return Box{lo<<factor_log2, hi<<factor_log2};
      else {
        int bias = (1<<-factor_log2)-1;
        return Box{lo>>-factor_log2, (hi+bias)>>-factor_log2};
      }
    }
    
    Box neighbor(Pt<int> side, int width) const {
      Box nbr;
      for(int d=0; d < 3; d++) {
        if(side[d] < 0) {
          nbr.lo[d] = lo[d]-width;
          nbr.hi[d] = lo[d];
        }
        else if(side[d] > 0) {
          nbr.lo[d] = hi[d];
          nbr.hi[d] = hi[d]+width;
        }
        else {
          nbr.lo[d] = lo[d];
          nbr.hi[d] = hi[d];
        }
      }
      return nbr;
    }
    
    // all neighbors, corners, faces etc. self not included
    std::array<Box,26> neighbors(int width) const {
      std::array<Box,26> a;
      int i = 0;
      Pt<int> side;
      
      for(side[0]=-1; side[0]<=1; side[0]++) {
        for(side[1]=-1; side[1]<=1; side[1]++) {
          for(side[2]=-1; side[2]<=1; side[2]++) {
            if(side != Pt<int>(0,0,0)) {
              a[i++] = neighbor(side, width);
            }
          }
        }
      }
      
      return a;
    }
    
    // just face neighbors
    std::array<Box,6> neighbors_2d(int width) const {
      std::array<Box,6> a;
      for(int d=0; d < 3; d++) {
        a[2*d+0] = *this;
        a[2*d+0].lo[d] = lo[d]-width;
        a[2*d+0].hi[d] = lo[d];
        
        a[2*d+1] = *this;
        a[2*d+1].lo[d] = hi[d];
        a[2*d+1].hi[d] = hi[d]+width;
      }
      return a;
    }
    
    // deque<Box> is always assumed to be self-disjoint
    
    static void intersect(std::deque<Box> &ans, const std::deque<Box> &as, const Box &b);
    static void intersect(std::deque<Box> &as, const Box &b);
    
    // subtract `b` from `as` inplace
    static void subtract(std::deque<Box> &as, const Box &b);
    // subtract `b` from `a`, put result boxes on back of `ans`
    static void subtract(std::deque<Box> &push_on, const Box &a, const Box &b);
    
    static void unify(std::deque<Box> &as, const Box &b);
    
    // make it so that every box in `as` is either completely subsumed by
    // or completely disjoint from `b` while preserving the set of points
    // represented by the union over `as`.
    static void split(std::deque<Box> &as, const Box &b);
  };

  inline std::ostream& operator<<(std::ostream &o, const Box &x) {
    return o << '{' <<
        x.lo[0]<<".."<<x.hi[0]<<", "<<
        x.lo[1]<<".."<<x.hi[1]<<", "<<
        x.lo[2]<<".."<<x.hi[2]<<
      '}';
  }
}}
namespace std {
  template<>
  struct hash<programr::amr::Box> {
    size_t operator()(const programr::amr::Box &x) const {
      return hash<array<int,6>>()({{
        x.lo.x[0], x.lo.x[1], x.lo.x[2],
        x.hi.x[0], x.hi.x[1], x.hi.x[2]
      }});
    }
  };
}
#endif

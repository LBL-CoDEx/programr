#ifndef _3264d027_0e09_4fcf_bd86_2bf0b719e87e
#define _3264d027_0e09_4fcf_bd86_2bf0b719e87e

# include "box.hxx"
# include "lowlevel/ref.hxx"

# include <deque>
# include <array>

namespace programr {
namespace amr {
  struct Boundary: Referent {
    /*const*/ Box domain;
    virtual void internalize(std::deque<Box> &ans, int scale_log2, const Box &box) const = 0;
    // overloads
    std::deque<Box> internalize(int scale_log2, const Box &box) const {
      std::deque<Box> ans;
      internalize(ans, scale_log2, box);
      return ans;
    }
    // TODO: template over any Iterable<Box> type
    std::deque<Box> internalize(int scale_log2, std::array<Box, 6> boxes) {
      std::deque<Box> inside;
      for (auto b : boxes) internalize(inside, scale_log2, b);
      return inside;
    }
  };
  
  struct BoundarySimple: Boundary {
    BoundarySimple(const Box &domain) {
      this->domain = domain;
    }
    void internalize(std::deque<Box> &ans, int scale_log2, const Box &box) const override {
      Box scaled_domain = this->domain.scaled_pow2(scale_log2);
      ans.push_back(Box::intersection(box, scaled_domain));
    }
  };

  struct BoundaryPeriodic: Boundary {
    BoundaryPeriodic(const Box &domain) {
      this->domain = domain;
    }
    void internalize(std::deque<Box> &ans, int scale_log2, const Box &box) const override {
      Box sd = this->domain.scaled_pow2(scale_log2);
      struct Interval {
        int lo, hi;
      };
      std::array<std::deque<Interval>, 3> intervals;
      for (int d = 0; d < 3; ++d) {
        if (box.hi[d] - box.lo[d] >= sd.size()[d]) {
          intervals[d].push_back( Interval{sd.lo[d], sd.hi[d]} );
        } else {
          // lambda to periodic-wrap x into [lo, hi)
          auto wrap_into = [](int x, int lo, int hi) {
            int d = hi-lo, t = (x-lo) % d;
            return lo + (t < 0 ? t+d : t);
          };
          int lo = wrap_into(box.lo[d], sd.lo[d]  , sd.hi[d]  ),
              hi = wrap_into(box.hi[d], sd.lo[d]+1, sd.hi[d]+1);
          if (lo <= hi) {
            intervals[d].push_back( Interval{lo, hi} );
          } else {
            intervals[d].push_back( Interval{sd.lo[d], hi} );
            intervals[d].push_back( Interval{lo, sd.hi[d]} );
          }
        }
      }
      for (auto int0 : intervals[0]) {
        for (auto int1 : intervals[1]) {
          for (auto int2 : intervals[2]) {
            ans.push_back( Box{ Pt<int>{ int0.lo, int1.lo, int2.lo },
                                Pt<int>{ int0.hi, int1.hi, int2.hi } } );
          }
        }
      }
    }
  };
}}
#endif

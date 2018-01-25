#ifndef _c2245f2f_fec3_4ee0_9342_4219f2b33fac
#define _c2245f2f_fec3_4ee0_9342_4219f2b33fac

# include "box.hxx"
# include "lowlevel/intset.hxx"
# include "lowlevel/ref.hxx"
# include "lowlevel/pile.hxx"
# include "lowlevel/digest.hxx"
# include "lowlevel/byteseq.hxx"

# include <cstdint>
# include <memory>
# include <tuple>
# include <vector>

namespace programr {
namespace amr {
  // Ordered list of boxes with no other constraints.
  // Spatial testing is accelerated by a binning hashtable.
  class BoxList: public Referent {
    // golden ratio for hashing
    static const std::size_t gold = 8*sizeof(size_t)==32 ? 0x9e3779b9u : 0x9e3779b97f4a7c15u;
    
    Digest<128> _digest;
    int _n;
    int _n_log2; // log2up(_n)
    Box *_boxes;
    
    struct Bin {
      std::size_t hash;
      ByteSeqPtr byteseq;
    };
    
    Pile _pile;
    // self table has 1<<_n_log2 buckets
    int *_self_bkts_off;
    int *_self_ixs;
    // bin table has 1<<(_n_log2 + _bin_more_log2) buckets
    static const int _bin_more_log2 = 0;
    int *_bin_bkts_off;
    Bin *_bins;
    Pt<std::int8_t> _bin_shift;
    
  private:
    int _bucket_of(std::size_t h, int bkt_n_log2) const;
    
    template<class F>
    bool _for_bins(const Box &x, const F &f_bkt_hash) const;
  
  private:
    BoxList(int box_n, Box *boxes);
  public:
    BoxList(int box_n, std::unique_ptr<Box[]> &&boxes):
      BoxList(box_n, boxes.release()) {
    }
    ~BoxList();
    
    static BoxList* nil();
    
    Digest<128> digest() const { return _digest; }
    
    bool operator==(const BoxList &that) const;
    bool operator!=(const BoxList &that) const { return !(*this == that); }
    
    int size() const { return _n; }
    
    const Box& operator[](int ix) const {
      return _boxes[ix];
    }
    
    int ix_of(const Box &box) const;
    
    // f_id_box is called on each box that has nonzero-intersection with
    // area, possibly multiple times, and possibly called on other boxes too.
    template<class F>
    bool for_near(const Box &area, const F &f_ix_box) const {
      return this->_for_bins(area,
        [&](int bkt, std::size_t h)->bool {
          int bin_off = _bin_bkts_off[bkt];
          int bin_off1 = _bin_bkts_off[bkt+1];
          
          while(bin_off < bin_off1) {
            Bin *bin = &_bins[bin_off];
            if(bin->hash == h) {
              return bin->byteseq.for_bit1(
                [&](int ix) {
                  return f_ix_box(ix, _boxes[ix]);
                }
              );
            }
            bin_off += 1;
          }
          
          return true;
        }
      );
    }
    
    // returns the box ids having nonzero-intersection with area.
    // excluded_ix is omitted from result set.
    IntSet<int> intersectors(const Box &area, int excluded_ix=-1) const;
  };
  
  
  inline int BoxList::_bucket_of(std::size_t h, int bkt_n_log2) const {
    h ^= h >> (4*sizeof(std::size_t) + 1);
    h *= gold;
    h = bkt_n_log2 == 0 ? 0 : h>>(8*sizeof(std::size_t) - bkt_n_log2);
    return int(h);
  }
  
  template<class F>
  bool BoxList::_for_bins(const Box &x, const F &f_bkt_hash) const {
    Pt<int> z0 = x.lo >> Pt<int>(_bin_shift);
    Pt<int> z1 = (x.hi + ((1 << Pt<int>(_bin_shift))-1)) >> Pt<int>(_bin_shift);
    Pt<int> z;
    
    for    (z.x[0] = z0.x[0]; z.x[0] < z1.x[0]; z.x[0]++) {
      for  (z.x[1] = z0.x[1]; z.x[1] < z1.x[1]; z.x[1]++) {
        for(z.x[2] = z0.x[2]; z.x[2] < z1.x[2]; z.x[2]++) {
          std::size_t h = std::hash<Pt<int>>()(z);
          if(!f_bkt_hash(_bucket_of(h, _n_log2 + _bin_more_log2), h))
            return false;
        }
      }
    }
    
    return true;
  }
}}

namespace std {
  template<>
  struct hash<programr::amr::BoxList> {
    size_t operator()(const programr::amr::BoxList &x) const {
      return hash<programr::Digest<128>>()(x.digest());
    }
  };
}

#endif

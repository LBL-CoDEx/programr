#include "boxlist.hxx"

#include "lowlevel/pile.hxx"
#include "lowlevel/spookyhash.hxx"

using namespace programr;
using namespace programr::amr;
using namespace std;

namespace {
  int log2_up(size_t x) {
    const size_t one = 1;
    int y = 0;
    while(one<<y < x)
      y++;
    return y;
  }
  /*int log2_dn(size_t x) {
    const size_t one = 1;
    int y = 0;
    while(one<<(y+1) <= x)
      y++;
    return y;
  }*/
}

BoxList* BoxList::nil() {
  static BoxList it(0, nullptr);
  return &it;
}

namespace {
  struct Bin1Node {
    Bin1Node *next;
    int hi;
    std::uint32_t lo_mask;
  };
  struct Bin1 {
    Bin1 *next;
    std::size_t hash;
    Bin1Node *nodes;
  };
  
  struct Self1 {
    Self1 *next;
    int ix;
  };
  
  void bin1_bucket_put(Bin1 **bkt, std::size_t h, int ix, int *bin_n, Pile *pile) {
    Bin1 *bin;
    for(bin=*bkt; bin != nullptr; bin = bin->next) {
      if(bin->hash == h)
        goto have_bin;
    }
    // no bin
    bin = pile->push<Bin1>();
    bin->next = *bkt;
    bin->hash = h;
    bin->nodes = nullptr;
    *bkt = bin;
    *bin_n += 1;
    
  have_bin:
    int ix_hi = ix & -32;
    int ix_lo = ix & 31;
    
    Bin1Node **p_nd = &bin->nodes;
    
    while(true) {
      Bin1Node *nd = *p_nd;
      if(nd == nullptr || nd->hi > ix_hi) {
        *p_nd = pile->push<Bin1Node>();
        (*p_nd)->next = nd;
        (*p_nd)->hi = ix_hi;
        (*p_nd)->lo_mask = std::uint32_t(1)<<ix_lo;
        break;
      }
      else if(nd->hi == ix_hi) {
        nd->lo_mask |= std::uint32_t(1)<<ix_lo;
        break;
      }
      else
        p_nd = &nd->next;
    }
  }
  
  void bin1_nodes_to_byteseq(Bin1Node *nds, ByteSeqBuilder &seq) {
    int hi_prev = 0;
    
    for(Bin1Node *nd = nds; nd != nullptr; nd = nd->next) {
      uint32_t lo = nd->lo_mask;
      int hi = nd->hi;
      
      seq.add_zeros((hi - hi_prev)/8);
      hi_prev = hi + 32;
      
      for(int i=0; i < 4; i++) {
        seq.add_byte(lo & 0xff);
        lo >>= 8;
      }
    }
  }
}

BoxList::BoxList(int box_n, Box *boxes):
  _n(box_n),
  _n_log2(log2_up(_n)),
  _boxes(boxes) {
  
  Pile pile1;
  
  { // _bin_shift, _digest, _self's
    SpookyHasher digester;
    uint64_t avg[3] = {0,0,0};
    
    Self1 **self1_bkts = new Self1*[1<<_n_log2](/*all nulls*/);
    
    for(int ix=0; ix < _n; ix++) {
      const Box &x = _boxes[ix];
      
      // _bin_shift
      avg[0] += x.hi[0] - x.lo[0];
      avg[1] += x.hi[1] - x.lo[1];
      avg[2] += x.hi[2] - x.lo[2];
      
      // _digest
      digester.consume(x);
      
      // _self's
      int bkt = _bucket_of(std::hash<Box>()(x), _n_log2);
      Self1 *self1 = pile1.push<Self1>();
      self1->next = self1_bkts[bkt];
      self1->ix = ix;
      self1_bkts[bkt] = self1;
    }
    
    // _bin_shift
    if(_n > 0) {
      _bin_shift = Pt<int8_t>(
        log2_up(avg[0]/_n)+0,
        log2_up(avg[1]/_n)+0,
        log2_up(avg[2]/_n)+0
      );
    }
    else
      _bin_shift = Pt<int8_t>(0);
    
    // _digest
    _digest = digester.digest();
    
    // _self's
    _self_ixs = new int[_n];
    _self_bkts_off = new int[1 + (1<<_n_log2)];
    int self_off = 0;
    for(int bkt=0; bkt < 1<<_n_log2; bkt++) {
      _self_bkts_off[bkt] = self_off;
      
      for(Self1 *self1=self1_bkts[bkt]; self1; self1 = self1->next)
        _self_ixs[self_off++] = self1->ix;
    }
    _self_bkts_off[1<<_n_log2] = self_off;
    
    delete[] self1_bkts;
  }
  
  pile1.chop(0);
  
  { // _bin's
    Bin1 **bin1_bkts = new Bin1*[1<<(_n_log2 + _bin_more_log2)](/*all nulls*/);
    for(int bkt=0; bkt < 1<<(_n_log2 + _bin_more_log2); bkt++)
      bin1_bkts[bkt] = nullptr;
    
    int bin_n = 0;
    
    // put each box into all binning buckets
    for(int ix=0; ix < _n; ix++) {
      _for_bins(_boxes[ix],
        [&](int bkt, size_t h)->bool {
          bin1_bucket_put(&bin1_bkts[bkt], h, ix, &bin_n, &pile1);
          return true;
        }
      );
    }
    
    // construct bin table
    ByteSeqBuilder seq_b;
    int bin_off = 0;
    
    _bin_bkts_off = new int[1 + (1<<(_n_log2 + _bin_more_log2))]; // 1 extra
    _bins = new Bin[bin_n];
    
    for(int bkt=0; bkt < 1<<(_n_log2 + _bin_more_log2); bkt++) {
      _bin_bkts_off[bkt] = bin_off;
      
      for(Bin1 *bin1=bin1_bkts[bkt]; bin1 != nullptr; bin1 = bin1->next) {
        Bin *bin = &_bins[bin_off++];
        bin->hash = bin1->hash;
        bin1_nodes_to_byteseq(bin1->nodes, seq_b);
        bin->byteseq = seq_b.finish([&](std::size_t sz) { return _pile.push<uint8_t>(sz); });
      }
    }
    
    _bin_bkts_off[1<<(_n_log2 + _bin_more_log2)] = bin_off; // set extra to delimit last bucket
    delete[] bin1_bkts;
  }
}

BoxList::~BoxList() {
  if(_boxes) delete[] _boxes;
  delete[] _self_bkts_off;
  delete[] _self_ixs;
  delete[] _bin_bkts_off;
  delete[] _bins;
}

bool BoxList::operator==(const BoxList &that) const {
  if(this->_digest != that._digest || this->_n != that._n)
    return false;
  
  for(int i=0; i < _n; i++) {
    if(this->_boxes[i] != that._boxes[i])
      return false;
  }
  
  return true;
}

int BoxList::ix_of(const Box &box) const {
  int bkt = _bucket_of(std::hash<Box>()(box), _n_log2);
  int off = _self_bkts_off[bkt];
  int off1 = _self_bkts_off[bkt+1];
  
  while(off < off1) {
    int ix = _self_ixs[off];
    if(box == _boxes[ix])
      return ix;
    off += 1;
  }
  
  return -1;
}

IntSet<int> BoxList::intersectors(const Box &area, int excluded_ix) const {
  IntSet<int> ans;
  
  for_near(area, [&](int ix, const Box &box)->bool {
    if(ix != excluded_ix && box.intersects(area))
      ans.put(ix);
    return true;
  });
  
  return ans;
}

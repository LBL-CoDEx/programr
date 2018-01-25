#ifndef _91bed093_8c9a_4294_8e94_d75e555c8f9f
#define _91bed093_8c9a_4294_8e94_d75e555c8f9f

# include "bitops.hxx"

# include <cstdint>
# include <iostream>
# include <vector>

namespace programr {
  struct ByteSeqPtr {
    std::uint8_t *ptr;
    
    template<class F>
    bool for_nonz(const F &f_ix_byte) const;
    
    template<class F>
    bool for_bit1(const F &f_ix) const;
  };
  
  class ByteSeqIter_ {
    std::uint8_t *_seq;
    int _ix;
    std::uint8_t _label;
    std::uint8_t _head;
    std::uint8_t _i;
    std::uint8_t _byte;
  public:
    ByteSeqIter_(ByteSeqPtr p):
      _seq(p.ptr),
      _ix(0),
      _label(0) {
    }
    
    // fill buffer with nonzero bytes and their indices, returns number put in buffer
    int next(int buf_sz, std::uint8_t *buf_byte, int *buf_ix);
  };
  
  template<int buf_sz>
  class ByteSeqIter: private ByteSeqIter_ {
    std::uint8_t _buf_byte[buf_sz];
    int _buf_ix[buf_sz];
    std::int16_t _p=buf_sz, _n=buf_sz;
  public:
    ByteSeqIter(ByteSeqPtr p): ByteSeqIter_(p) {}
    
    int ix() const { return _buf_ix[_p]; }
    std::uint8_t byte() const { return _buf_byte[_p]; }
    
    bool next() {
      _p += 1;
      if(_p < _n)
        return true;
      else {
        if(_n != buf_sz)
          return false;
        _n = ByteSeqIter_::next(buf_sz, _buf_byte, _buf_ix);
        _p = 0;
        return _n != 0;
      }
    }
  };
  
  template<int buf_sz>
  class ByteSeq1BitIter: private ByteSeqIter<buf_sz> {
    std::uint8_t _byte;
    std::uint8_t _b;
  public:
    ByteSeq1BitIter(ByteSeqPtr p): ByteSeqIter<buf_sz>(p) {}
    
    int ix() const { return 8*ByteSeqIter<buf_sz>::ix() + _b; }
    
    bool next() {
      if(_byte == 0) {
        if(!ByteSeqIter<buf_sz>::next())
          return false;
        _byte = ByteSeqIter<buf_sz>::byte();
      }
      _b = bitffs(_byte) - 1;
      _byte &= _byte-1;
      return true;
    }
  };
  
  // builds a compressed sequence of sparse (mostly zero) bytes
  class ByteSeqBuilder {
    std::vector<std::uint8_t> _bytes;
    std::uint8_t _head;
    std::uint8_t _head_bit_n;
    int _head_pos;
    int _zeros_n;
  
  public:
    ByteSeqBuilder() noexcept:
      _bytes(1),
      _head(0),
      _head_bit_n(0),
      _head_pos(0),
      _zeros_n(0) {
    }
    ByteSeqBuilder(ByteSeqBuilder&&) noexcept = default;
    ByteSeqBuilder& operator=(ByteSeqBuilder&&) noexcept = default;
    
  private:
    void _push_head(int bit);
    void _push_int(unsigned n, std::uint8_t first, int first_bits);
    
  public:
    void add_byte(std::uint8_t byte);
    void add_zeros(int byte_n);
    
    template<class F>
    ByteSeqPtr finish(const F &alloc);
  };
  
  
  inline void ByteSeqBuilder::_push_head(int bit) {
    if(_head_bit_n == 8) {
      _bytes[_head_pos] = _head;
      _head = 0;
      _head_bit_n = 0;
      _head_pos = _bytes.size();
      _bytes.push_back(0);
    }
    _head |= bit<<_head_bit_n;
    _head_bit_n += 1;
  }
  
  inline void ByteSeqBuilder::_push_int(unsigned n, std::uint8_t first, int first_bits) {
    std::uint8_t byte = first;
    int bits = first_bits;
    while(true) {
      std::uint8_t hibit = 1<<(bits-1);
      byte |= n & (hibit-1);
      n >>= bits-1;
      byte |= n!=0 ? hibit : 0;
      _bytes.push_back(byte);
      if(n == 0) break;
      byte = 0;
      bits = 8;
    }
  }
  
  inline void ByteSeqBuilder::add_byte(std::uint8_t byte) {
    if(byte == 0)
      add_zeros(1);
    else {
      if(_zeros_n > 0 && (byte & (byte-1)) == 0) { // zeros followed by one bit
        int bit = bitffs(byte) - 1;
        _push_head(0);
        _push_int(_zeros_n-1, 0x80 | bit<<4, 4);
        _zeros_n = 0;
      }
      else {
        if(_zeros_n > 0) {
          _push_head(0);
          _push_int(_zeros_n-1, 0x00, 7);
          _zeros_n = 0;
        }
        _push_head(1);
        _bytes.push_back(byte);
      }
    }
  }
  
  inline void ByteSeqBuilder::add_zeros(int byte_n) {
    _zeros_n += byte_n;
  }
  
  template<class F>
  ByteSeqPtr ByteSeqBuilder::finish(const F &alloc) {
    // add terminating byte
    _push_head(1);
    _bytes.push_back(0);
    _bytes[_head_pos] = _head;
    
    std::uint8_t *p = alloc(_bytes.size());
    int n = _bytes.size();
    
    for(int i=0; i < n; i++)
      p[i] = _bytes[i];
    
    // reset
    _bytes.resize(1);
    _head = 0;
    _head_bit_n = 0;
    _head_pos = 0;
    _zeros_n = 0;
    
    return ByteSeqPtr{p};
  }
  
  template<class F>
  bool ByteSeqPtr::for_nonz(const F &f_ix_byte) const {
# if 1 // use the iterator to minimize amount of templated-in code bloat
    ByteSeqIter<16> it(*this);
    while(it.next())
      if(!f_ix_byte(it.ix(), it.byte()))
        return false;
    return true;
# else // leave around for documentation, iterator source is hard to read
    const std::uint8_t *seq = this->ptr;
    int ix = 0; // decoded byte index
    
    while(true) {
      std::uint8_t head = *seq++;
      
      for(int i=0; i < 8; i++) {
        int head_bit = head & 1;
        head >>= 1;
        std::uint8_t byte = *seq++;
        
        if(head_bit == 1 && byte == 0)
          return true; // eof
        
        if(head_bit == 0) {
          std::uint8_t chunk = byte;
          int chunk_bits;
          if(byte & 0x80) {
            chunk_bits = 4;
            byte = 1<<(byte>>4 & 0x07);
            ix += 1;
          }
          else {
            chunk_bits = 7;
            byte = 0;
          }
          
          int mag = 0;
          while(true) {
            ix += (chunk & ((1<<(chunk_bits-1))-1)) << mag;
            mag += chunk_bits-1;
            if(0 == (chunk & (1<<(chunk_bits-1))))
              break;
            chunk = *seq++;
            chunk_bits = 8;
          }
        }
        
        if(byte != 0)
          if(!f_ix_byte(ix, byte))
            return false;
        
        ix += 1;
      }
    }
# endif
  }
  
  template<class F>
  bool ByteSeqPtr::for_bit1(const F &f_ix) const {
    return this->for_nonz(
      [&](int ix, std::uint8_t byte)->bool {
        while(byte != 0) {
          int b = bitffs(byte) - 1;
          byte &= byte-1;
          if(!f_ix(8*ix + b))
            return false;
        }
        return true;
      }
    );
  }
}
#endif

#ifndef _68b758dc_fb53_4b98_a467_40c306743892
#define _68b758dc_fb53_4b98_a467_40c306743892

# include <cstdint>
# include <functional>
# include <iostream>

namespace programr {
  inline std::uint32_t bitmix(std::uint32_t x) {
    x ^= x >> 16;
    x *= 0x85ebca6b;
    x ^= x >> 13;
    x *= 0xc2b2ae35;
    x ^= x >> 16;
    return x;
  }
  
  inline std::uint64_t bitmix(std::uint64_t x) {
    x ^= x >> 33;
    x *= 0xff51afd7ed558ccd;
    x ^= x >> 33;
    x *= 0xc4ceb9fe1a85ec53;
    x ^= x >> 33;
    return x;
  }
  
  class SeqHash {
    std::size_t _h;
  public:
    SeqHash(std::size_t seed=0):
      _h(seed) {
    }
    template<class T>
    SeqHash(const T &seed):
      _h(std::hash<T>()(seed)) {
    }
    
    template<class T>
    SeqHash& consume(const T &x) {
      const std::size_t gold = 8*sizeof(size_t)==32 ? 0x9e3779b9u : 0x9e3779b97f4a7c15u;
      _h ^= _h >> (4*sizeof(_h)+1);
      _h *= gold;
      _h += std::hash<T>()(x);
      return *this;
    }
    
    std::size_t hash() const {
      const std::size_t gold = 8*sizeof(size_t)==32 ? 0x9e3779b9u : 0x9e3779b97f4a7c15u;
      std::size_t h = _h;
      h ^= h >> (4*sizeof(h)+1);
      h *= gold;
      return h;
    }
  };
  
  // a digest is a POD type representing 'bit_n' uniform random bits
  template<int bit_n>
  struct Digest;
  
  template<>
  struct Digest<128> {
    std::uint64_t w0, w1;
    
    bool operator==(const Digest<128> &that) const {
      return this->w0 == that.w0 && this->w1 == that.w1;
    }
    bool operator!=(const Digest<128> &that) const {
      return this->w0 != that.w0 || this->w1 != that.w1;
    }
    
    Digest<128>& operator^=(const Digest<128> &that) {
      this->w0 ^= that.w0;
      this->w1 ^= that.w1;
      return *this;
    }
    Digest<128> operator^(const Digest<128> &that) const {
      Digest<128> t = *this;
      return t ^= that;
    }
    
    Digest<128>& operator+=(const Digest<128> &that) {
      std::uint32_t *a = reinterpret_cast<std::uint32_t*>(this);
      const std::uint32_t *b = reinterpret_cast<const std::uint32_t*>(&that);
      
      std::uint64_t carry = 0;
      for(int i=0; i < 4; i++) {
        carry += a[i];
        carry += b[i];
        a[i] = std::uint32_t(carry);
        carry >>= 32;
      }
      
      return *this;
    }
    Digest<128> operator+(const Digest<128> &that) const {
      Digest<128> t = *this;
      return t += that;
    }
    
    Digest<128>& operator-=(const Digest<128> &that) {
      std::uint32_t *a = reinterpret_cast<std::uint32_t*>(this);
      const std::uint32_t *b = reinterpret_cast<const std::uint32_t*>(&that);
      
      std::uint64_t carry = 1;
      for(int i=0; i < 4; i++) {
        carry += a[i];
        carry += ~b[i];
        a[i] = std::uint32_t(carry);
        carry >>= 32;
      }
      
      return *this;
    }
    Digest<128> operator-(const Digest<128> &that) const {
      Digest<128> t = *this;
      return t -= that;
    }
  };
  
  template<int bit_n>
  inline std::size_t hash_mod_2n(int n, const Digest<bit_n> &x) {
    return n == 0 ? 0 : x.w0 >> (8*sizeof(x.w0) - n);
  }
  
  template<int bit_n>
  inline std::ostream& operator<<(std::ostream &o, const Digest<bit_n> &x) {
    const char *hex = "0123456789abcdef";
    const unsigned char *s = reinterpret_cast<const unsigned char*>(&x);
    
    char buf[2*sizeof(x) + 1];
    int p = 0;
    
    for(int i=0; i < (int)sizeof(x); i++) {
      buf[p++] = hex[s[i]>>4];
      buf[p++] = hex[s[i] & 0xf];
    }
    buf[p] = '\0';
    
    return o << buf;
  }
}

namespace std {
  template<>
  struct hash<programr::Digest<128>> {
    size_t operator()(programr::Digest<128> x) const {
      return size_t(x.w0);
    }
  };
}
#endif

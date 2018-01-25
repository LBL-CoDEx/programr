#ifndef _24e7270f_ad74_4c29_a4f0_3452924e1ee3
#define _24e7270f_ad74_4c29_a4f0_3452924e1ee3

# include <cstdint>

namespace programr {
  template<int bit_n> struct IntGoldenRatio;
  
  template<> struct IntGoldenRatio<32> {
    static constexpr std::uint32_t value = 0x9e3779b9u;
  };
  template<> struct IntGoldenRatio<64> {
    static constexpr std::uint64_t value = 0x9e3779b97f4a7c15u;
  };
  
  // Unsigned<T>::type maps T to its unsigned counterpart.
  template<class T> struct Unsigned; 
  
  template<> struct Unsigned<char>          { typedef unsigned char type; };
  template<> struct Unsigned<unsigned char> { typedef unsigned char type; };
  template<> struct Unsigned<signed char>   { typedef unsigned char type; };
  
  template<> struct Unsigned<unsigned short> { typedef unsigned short type; };
  template<> struct Unsigned<signed short>   { typedef unsigned short type; };
  
  template<> struct Unsigned<unsigned int> { typedef unsigned int type; };
  template<> struct Unsigned<signed int>   { typedef unsigned int type; };
  
  template<> struct Unsigned<unsigned long> { typedef unsigned long type; };
  template<> struct Unsigned<signed long>   { typedef unsigned long type; };
  
  template<> struct Unsigned<unsigned long long> { typedef unsigned long long type; };
  template<> struct Unsigned<signed long long>   { typedef unsigned long long type; };
  
  inline int bitffs(int x)                { return __builtin_ffs(x); }
  inline int bitffs(unsigned int x)       { return __builtin_ffs(x); }
  inline int bitffs(long x)               { return __builtin_ffsl(x); }
  inline int bitffs(unsigned long x)      { return __builtin_ffsl(x); }
  inline int bitffs(long long x)          { return __builtin_ffsll(x); }
  inline int bitffs(unsigned long long x) { return __builtin_ffsll(x); }
  
  inline int bitlog2dn(unsigned int x)       { return x == 0 ? -1 : 8*sizeof(int)-1       - __builtin_clz(x); }
  inline int bitlog2dn(unsigned long x)      { return x == 0 ? -1 : 8*sizeof(long)-1      - __builtin_clzl(x); }
  inline int bitlog2dn(unsigned long long x) { return x == 0 ? -1 : 8*sizeof(long long)-1 - __builtin_clzll(x); }  
  
  inline int bitlog2up(unsigned int x)       { return x <= 1 ? 0 : 8*sizeof(int)       - __builtin_clz(x-1); }
  inline int bitlog2up(unsigned long x)      { return x <= 1 ? 0 : 8*sizeof(long)      - __builtin_clzl(x-1); }
  inline int bitlog2up(unsigned long long x) { return x <= 1 ? 0 : 8*sizeof(long long) - __builtin_clzll(x-1); }  
}

#endif

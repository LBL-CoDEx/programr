#ifndef _78da547b_eb76_487c_8572_b6a0549a40bd
#define _78da547b_eb76_487c_8572_b6a0549a40bd

# include "typeclass.hxx"

# include <array>
# include <iostream>

namespace programr {
namespace amr {
  template<class T>
  struct Pt {
    T x[3];
    
    Pt(const T &s=0) {
      x[0] = s; x[1] = s; x[2] = s;
    }
    Pt(T x0, T x1, T x2) {
      x[0] = x0; x[1] = x1; x[2] = x2;
    }
    
    Pt(const Pt<T> &that) {
      this->x[0] = that.x[0];
      this->x[1] = that.x[1];
      this->x[2] = that.x[2];
    }
    template<class U>
    Pt(const Pt<U> &that) {
      this->x[0] = that.x[0];
      this->x[1] = that.x[1];
      this->x[2] = that.x[2];
    }
    
    T operator[](std::size_t ix) const { return x[ix]; }
    T& operator[](std::size_t ix) { return x[ix]; }

    inline friend bool operator==(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] == b.x[0] && a.x[1] == b.x[1] && a.x[2] == b.x[2];
    }
    inline friend bool operator!=(const Pt<T> &a, const Pt<T> &b) {
      return !(a == b);
    }
    
    inline friend bool all_lt(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] < b.x[0] && a.x[1] < b.x[1] && a.x[2] < b.x[2];
    }
    inline friend bool all_gt(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] > b.x[0] && a.x[1] > b.x[1] && a.x[2] > b.x[2];
    }
    inline friend bool all_le(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] <= b.x[0] && a.x[1] <= b.x[1] && a.x[2] <= b.x[2];
    }
    inline friend bool all_ge(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] >= b.x[0] && a.x[1] >= b.x[1] && a.x[2] >= b.x[2];
    }
    
    inline friend bool any_lt(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] < b.x[0] || a.x[1] < b.x[1] || a.x[2] < b.x[2];
    }
    inline friend bool any_gt(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] > b.x[0] || a.x[1] > b.x[1] || a.x[2] > b.x[2];
    }
    inline friend bool any_le(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] <= b.x[0] || a.x[1] <= b.x[1] || a.x[2] <= b.x[2];
    }
    inline friend bool any_ge(const Pt<T> &a, const Pt<T> &b) {
      return a.x[0] >= b.x[0] || a.x[1] >= b.x[1] || a.x[2] >= b.x[2];
    }
    
    inline friend Pt<T> max(const Pt<T> &a, const Pt<T> &b) {
      using std::max;
      return Pt<T>(
        max(a.x[0], b.x[0]),
        max(a.x[1], b.x[1]),
        max(a.x[2], b.x[2])
      );
    }
    inline friend Pt<T> min(const Pt<T> &a, const Pt<T> &b) {
      using std::min;
      return Pt<T>(
        min(a.x[0], b.x[0]),
        min(a.x[1], b.x[1]),
        min(a.x[2], b.x[2])
      );
    }
    
    inline friend Pt<T>& operator+=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] += b.x[0]; a.x[1] += b.x[1]; a.x[2] += b.x[2];
      return a;
    }
    inline friend Pt<T> operator+(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y += b;
    }
    
    inline friend Pt<T>& operator-=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] -= b.x[0]; a.x[1] -= b.x[1]; a.x[2] -= b.x[2];
      return a;
    }
    inline friend Pt<T> operator-(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y -= b;
    }
    
    inline friend Pt<T>& operator*=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] *= b.x[0]; a.x[1] *= b.x[1]; a.x[2] *= b.x[2];
      return a;
    }
    inline friend Pt<T> operator*(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y *= b;
    }
    
    inline friend Pt<T>& operator/=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] /= b.x[0]; a.x[1] /= b.x[1]; a.x[2] /= b.x[2];
      return a;
    }
    inline friend Pt<T> operator/(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y /= b;
    }
    
    inline friend Pt<T>& operator%=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] %= b.x[0]; a.x[1] %= b.x[1]; a.x[2] %= b.x[2];
      return a;
    }
    inline friend Pt<T> operator%(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y %= b;
    }
    
    inline friend Pt<T>& operator<<=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] <<= b.x[0]; a.x[1] <<= b.x[1]; a.x[2] <<= b.x[2];
      return a;
    }
    inline friend Pt<T> operator<<(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y <<= b;
    }
    
    inline friend Pt<T>& operator>>=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] >>= b.x[0]; a.x[1] >>= b.x[1]; a.x[2] >>= b.x[2];
      return a;
    }
    inline friend Pt<T> operator>>(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y >>= b;
    }
    
    inline friend Pt<T>& operator&=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] &= b.x[0]; a.x[1] &= b.x[1]; a.x[2] &= b.x[2];
      return a;
    }
    inline friend Pt<T> operator&(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y &= b;
    }
    
    inline friend Pt<T>& operator|=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] |= b.x[0]; a.x[1] |= b.x[1]; a.x[2] |= b.x[2];
      return a;
    }
    inline friend Pt<T> operator|(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y |= b;
    }
    
    inline friend Pt<T>& operator^=(Pt<T> &a, const Pt<T> &b) {
      a.x[0] ^= b.x[0]; a.x[1] ^= b.x[1]; a.x[2] ^= b.x[2];
      return a;
    }
    inline friend Pt<T> operator^(const Pt<T> &a, const Pt<T> &b) {
      Pt<T> y = a; return y ^= b;
    }
  };
  
  
  template<class T>
  inline std::ostream& operator<<(std::ostream &o, const Pt<T> &x) {
    return o << '('<<x[0]<<','<<x[1]<<','<<x[2]<<')';
  }
}}
namespace std {
  template<class T>
  struct hash<programr::amr::Pt<T>> {
    size_t operator()(const programr::amr::Pt<T> &x) const {
      return hash<array<T,3>>()({{x[0], x[1], x[2]}});
    }
  };
}
#endif

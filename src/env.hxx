#ifndef _65a44e7e_d581_4f29_8c42_bb07eadde7f7
#define _65a44e7e_d581_4f29_8c42_bb07eadde7f7

#include <string>
#include <sstream>

namespace programr {
  template<class T>
  T c_str_to(const char *str) {
    T val; {
      std::istringstream ss{str};
      ss >> val;
    }
    return val;
  }

  template<class T>
  T env(std::string envkey, T defval) {
    const char *str = std::getenv(envkey.c_str());
    return str ? c_str_to<T>(str) : defval;
  }
}
#endif

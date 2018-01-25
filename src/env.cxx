#include "env.hxx"

using namespace std;

namespace programr {
  template<>
  string c_str_to<string>(const char *str) {
    return static_cast<string>(str);
  }
}

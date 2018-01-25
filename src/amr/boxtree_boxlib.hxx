#ifndef _152b4246_126c_4412_a8a2_0b1ce7091caa
#define _152b4246_126c_4412_a8a2_0b1ce7091caa

# include "boxtree.hxx"
# include "boxmap.hxx"

namespace programr {
namespace amr {
namespace boxtree {
  std::tuple<std::vector<Level>,Box,std::vector<Ref<BoxMap<int>>>>
  load_boxlib(const char *file);
}}}
#endif

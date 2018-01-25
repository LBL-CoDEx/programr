#ifndef _55ddce67_c17d_4a94_bb2e_c42d023e9fc8
#define _55ddce67_c17d_4a94_bb2e_c42d023e9fc8

# include "lowlevel/ref.hxx"

# include <cstdint>

namespace programr {
  struct Data: Referent {
    static std::uint64_t _id_next;
    
    const std::uint64_t id;
    std::function<void(Data*)> retirer;
    
    Data(): id(_id_next++) {}
    ~Data() { if(retirer) retirer(this); }
  };
}
#endif

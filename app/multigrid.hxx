#ifndef _1bccb0db_37a6_4709_84ad_a30a342318d7
#define _1bccb0db_37a6_4709_84ad_a30a342318d7

#include "amr/slab.hxx"
#include "list.hxx"

namespace multigrid {
  const int ADVANCE_HALO = 1;
  const int PROLONG_HALO = 1;
  const int BCGS_HALO = 1;
  const int MG_HALO = 1;
  const int NU = 4;
  const int MG_COARSEN_MIN = 2;

  enum class MGType {
    vcycle = 0,
    fcycle = 1,
  };

  programr::IList<programr::Ex<programr::amr::Slab>>
  mg_solve( MGType mg_type,
            programr::IList<programr::Ex<programr::amr::Slab>> rhs,
            programr::Ex<programr::amr::Slab> bottom_x );
};

#endif

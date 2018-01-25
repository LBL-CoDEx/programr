#include "boxtree.hxx"
#include "boxmemo.hxx"
#include "lowlevel/memo.hxx"

using namespace programr;
using namespace programr::amr;
using namespace programr::amr::boxtree;
using namespace std;

tuple<vector<Level>, Box>
boxtree::make_octree_full(
    int lev_n,
    int size
  ) {
  
  vector<Level> levels(lev_n);
  
  for(int lev=0; lev < lev_n; lev++) {
    unique_ptr<Box[]> boxes(new Box[1<<(3*lev)]);
    
    Pt<int> orig = 0<<lev;
    
    int ix = 0;
    for(int i=0; i < 1<<lev; i++) {
      for(int j=0; j < 1<<lev; j++) {
        for(int k=0; k < 1<<lev; k++) {
          boxes[ix++] = Box{
            orig + Pt<int>((i+0)*size, (j+0)*size, (k+0)*size),
            orig + Pt<int>((i+1)*size, (j+1)*size, (k+1)*size)
          };
        }
      }
    }
    
    levels[lev] = Level{
      /*boxes*/new BoxList(ix, std::move(boxes)),
      /*cell_scale_log2*/lev,
      /*box_scale_log2*/lev
    };
  };
  
  return make_tuple(
    std::move(levels),
    Box{Pt<int>(0), Pt<int>(size)}
  );
}

tuple<vector<Level>, Box>
boxtree::make_telescope(
    int lev_n,
    int dim_box_n,
    int box_size
  ) {

  vector<Level> levels(lev_n);
  
  int dim_size = dim_box_n*box_size;
  
  for(int lev=0; lev < lev_n; lev++) {
    unique_ptr<Box[]> boxes(new Box[dim_box_n*dim_box_n*dim_box_n]);
    
    Pt<int> orig = Pt<int>((dim_size<<lev)/2 - dim_size/2);
    
    int ix = 0;
    for(int i=0; i < dim_box_n; i++) {
      for(int j=0; j < dim_box_n; j++) {
        for(int k=0; k < dim_box_n; k++) {
          boxes[ix++] = Box{
            orig + Pt<int>((i+0)*box_size, (j+0)*box_size, (k+0)*box_size),
            orig + Pt<int>((i+1)*box_size, (j+1)*box_size, (k+1)*box_size)
          };
          //cout << boxes[ix-1] << '\n';
        }
      }
    }
    //cout << "----\n";

    levels[lev] = Level{
      /*boxes*/new BoxList(ix, std::move(boxes)),
      /*cell_scale_log2*/lev,
      /*box_scale_log2*/lev
    };
  }
  
  return make_tuple(
    std::move(levels),
    Box{Pt<int>(0), Pt<int>(dim_size)}
  );
}


////////////////////////////////////////////////////////////////////////
// boxtree::coarsened

namespace {
  Level _coarsen(
      Imm<BoxList> lev_boxes,
      int8_t lev_cell_s, // cell_scale_log2
      int8_t lev_box_s, // box_scale_log2
      int8_t ds // factor_log2
    ) {
    
    int lomask = (1<<(ds + lev_box_s-lev_cell_s))-1;
    int n0 = lev_boxes->size();
    int n1 = 0;
    
    int lev1_cell_s = lev_cell_s - ds;
    int lev1_box_s  = lev_cell_s - ds;
    
    // first pass: count boxes and determine lev1_box_s
    for(int i=0; i < n0; i++) {
      Box box = (*lev_boxes)[i];
      if((box.size() & lomask) != 0)
        /*bad size, omit box*/;
      else {
        n1 += 1; // include box
        if((box.lo & lomask) != 0)
          lev1_box_s = lev_box_s; // cant scale box
      }
    }
    
    // second pass: build result boxes
    unique_ptr<Box[]> lev1_boxes(new Box[n1]);
    n1 = 0;
      
    for(int i=0; i < n0; i++) {
      Box box = (*lev_boxes)[i];
      if((box.size() & lomask) != 0)
        /*skip box*/;
      else {
        lev1_boxes[n1++] = Box{
          box.lo>>(lev_box_s - lev1_box_s),
          box.hi>>(lev_box_s - lev1_box_s)
        };
      }
    }
    
    return Level{
      new BoxList(n1, std::move(lev1_boxes)),
      lev1_cell_s,
      lev1_box_s
    };
  }
  
  auto _m_coarsen = memoize(_coarsen);
}

Level boxtree::coarsened(const Level &lev, int factor_log2) {
  return _m_coarsen(lev.boxes, lev.cell_scale_log2, lev.box_scale_log2, factor_log2);
}


////////////////////////////////////////////////////////////////////////
// boxtree::siblings

namespace {
  uint8_t* _siblings(
      const function<uint8_t*(size_t)> &alloc,
      Imm<BoxList> lev_boxes,
      int ix,
      int8_t lev_cell_s,
      int8_t lev_box_s,
      Ref<Boundary> bdry
    ) {
    Box box = (*lev_boxes)[ix];
    
    // inflate box by 1 and then map to the domain's interior
    int cell = 1<<(lev_box_s - lev_cell_s);
    deque<Box> inside = bdry->internalize(lev_box_s, box.inflated(cell));
    
    IntSet<int> nbr_ixs;
    for(const Box &x: inside)
      nbr_ixs |= lev_boxes->intersectors(x, /*excluded_ix=*/ix);
    
    return nbr_ixs.as_byteseq().finish(alloc).ptr;
  }
  
  auto _m_siblings = boxmemoize_bytes(_siblings);
}

ByteSeqPtr boxtree::siblings(const Level &lev, int ix, Boundary *bdry) {
  return ByteSeqPtr{_m_siblings(lev.boxes, ix, lev.cell_scale_log2, lev.box_scale_log2, bdry)};
}


////////////////////////////////////////////////////////////////////////
// boxtree::parents

namespace {
  uint8_t* _parents(
      const function<uint8_t*(size_t)> &alloc,
      Imm<BoxList> kids,
      int kid_ix,
      bool neighboring,
      int8_t kids_cell_s, int8_t kids_box_s,
      int8_t pars_cell_s, int8_t pars_box_s,
      Imm<BoxList> pars,
      Ref<Boundary> bdry
    ) {
    Box kid_box = (*kids)[kid_ix];
    Box par_box = kid_box.scaled_pow2(pars_box_s - kids_box_s);
    
    if(neighboring) {
      par_box = par_box.inflated(1<<(pars_box_s - pars_cell_s));
      deque<Box> inside = bdry->internalize(pars_box_s, par_box);
      
      IntSet<int> ixs;
      for(const Box &x: inside)
        ixs |= pars->intersectors(x);
      
      return ixs.as_byteseq().finish(alloc).ptr;
    }
    else
      return pars->intersectors(par_box).as_byteseq().finish(alloc).ptr;
  }
  
  auto _m_parents = boxmemoize_bytes(_parents);
}

ByteSeqPtr boxtree::parents(
    const Level &kids,
    const Level &pars,
    int kid_ix,
    Boundary *bdry,
    bool neighboring
  ) {
  return ByteSeqPtr{_m_parents(
    kids.boxes, kid_ix, neighboring,
    kids.cell_scale_log2, kids.box_scale_log2,
    pars.cell_scale_log2, pars.box_scale_log2,
    pars.boxes, bdry
  )};
}


////////////////////////////////////////////////////////////////////////
// boxtree::children

namespace {
  uint8_t* _children(
      const function<uint8_t*(size_t)> &alloc,
      Imm<BoxList> pars,
      int par_ix,
      int delta_box_s,
      Imm<BoxList> kids
    ) {
    Box shadow = (*pars)[par_ix].scaled_pow2(delta_box_s);
    return kids->intersectors(shadow).as_byteseq().finish(alloc).ptr;
  }
  
  auto _m_children = boxmemoize_bytes(_children);
}

ByteSeqPtr boxtree::children(
    const Level &kids,
    const Level &pars,
    int par_ix
  ) {
  int delta_box_s = kids.box_scale_log2 - pars.box_scale_log2;
  return ByteSeqPtr{_m_children(pars.boxes, par_ix, delta_box_s, kids.boxes)};
}


////////////////////////////////////////////////////////////////////////
// boxtree::deps_halo

namespace {
  vector<tuple<int/*lev=0,-1*/, int/*box_ix*/, Box>>
  _deps_halo(
      Imm<BoxList> kids,
      int kid_ix,
      int8_t kids_cell_s, int8_t kids_box_s,
      int8_t pars_cell_s, int8_t pars_box_s,
      int8_t halo,
      int8_t prolong_halo,
      Imm<BoxList> pars,
      Ref<Boundary> bdry,
      bool flag_faces_only
    ) {
    
    vector<tuple<int/*lev*/,int/*box_i*/,Box>> ans;
    
    Box kid_box = (*kids)[kid_ix];
    
    // inflate kid_box by halo and then map to the domain's interior
    deque<Box> inside;
    if (flag_faces_only) {
      auto kid_fat = kid_box.inflated_faces((int)halo<<(kids_box_s-kids_cell_s));
      inside = bdry->internalize(kids_box_s, kid_fat);
    } else {
      Box kid_fat = kid_box.inflated((int)halo<<(kids_box_s-kids_cell_s));
      inside = bdry->internalize(kids_box_s, kid_fat);
    }
    deque<Box> gaps = inside;
    
    //cout << "halo kidbox " << kid_box << '\n';
    
    // walk siblings
    boxtree::siblings(
      Level{kids, kids_cell_s, kids_box_s},
      kid_ix, bdry
    )
    .for_bit1(
      [&](int sib_ix)->bool {
        Box sib_box = (*kids)[sib_ix];
        //cout << " sibbox " << sib_box << '\n';
        for(const Box &x: inside) {
          Box z = Box::intersection(x, sib_box);
          if(!z.is_empty()) {
            ans.push_back(make_tuple(0, sib_ix, z));
            Box::subtract(gaps, z);
          }
        }
        return true;
      }
    );
    
    if(pars) {
      // remove kid from gaps before ascending
      Box::subtract(gaps, kid_box);
      
      deque<Box> par_gaps; // gaps projected to coarser level, inflated by prolong_halo, then unioned
      for(Box x: gaps) {
        // project boxes onto parent level
        x = x.scaled_pow2(pars_box_s - kids_box_s);
        
        if(prolong_halo != 0) {
          // inflate by prolong halo
          x = x.inflated((int)prolong_halo<<(pars_box_s-pars_cell_s));
          // add to par_gaps
          Box::unify(par_gaps, x);
        }
        else // performance shortcut
          par_gaps.push_back(x);
      }
      
      // walk over parents of kid
      boxtree::parents(
        Level{kids, kids_cell_s, kids_box_s},
        Level{pars, pars_cell_s, pars_box_s},
        kid_ix, bdry, /*neighboring=*/true
      )
      .for_bit1([&](int par_ix)->bool {
        Box par_box = (*pars)[par_ix];
        //cout << " parbox " << par_box << '\n';
        for(Box x: par_gaps) {
          Box z = Box::intersection(x, par_box);
          if(!z.is_empty()) {
            //cout << "  isect " << z << '\n';
            ans.push_back(make_tuple(-1, par_ix, z));
          }
        }
        return true;
      });
    }
    
    return ans;
  }
}

vector<tuple<int,int,Box>>
boxtree::deps_halo(
    const Level &kids,
    const Level *pars,
    int kid_ix, // child level box index
    int halo,
    Boundary *bdry,
    int prolong_halo,
    bool flag_faces_only
  ) {
  return _deps_halo(
    kids.boxes, kid_ix,
    kids.cell_scale_log2, kids.box_scale_log2,
    pars ? pars->cell_scale_log2 : 0,
    pars ? pars->box_scale_log2 : 0,
    halo, prolong_halo,
    pars ? pars->boxes : nullptr,
    bdry,
    flag_faces_only
  );
}


////////////////////////////////////////////////////////////////////////
// boxtree::deps_restrict

vector<pair<int/*box_ix*/,Box>>
boxtree::deps_restrict(
    const Level &kids,
    const Level &pars,
    int par_ix
  ) {
  vector<pair<int/*box_ix*/,Box>> ans;
  Box shadow = (*pars.boxes)[par_ix].scaled_pow2(kids.box_scale_log2 - pars.box_scale_log2);
  
  // walk children
  boxtree::children(kids, pars, par_ix)
  .for_bit1([&](int kid_ix)->bool {
    Box kid_box = (*kids.boxes)[kid_ix];
    Box z = Box::intersection(kid_box, shadow);
    if(!z.is_empty())
      ans.push_back(make_pair(kid_ix, z));
    return true;
  });
  
  return ans;
}


////////////////////////////////////////////////////////////////////////
// boxtree::deps_prolong

vector<pair<int/*box_ix*/,Box>>
boxtree::deps_prolong(
    const Level &kids,
    const Level &pars,
    int kid_ix,
    Boundary *bdry,
    int interp_halo
  ) {
  
  vector<pair<int/*box_ix*/,Box>> ans;
  
  Box shadow = (*kids.boxes)[kid_ix].scaled_pow2(pars.box_scale_log2 - kids.box_scale_log2);
  shadow = shadow.inflated(interp_halo<<(pars.box_scale_log2 - pars.cell_scale_log2));
  
  boxtree::parents(kids, pars, kid_ix, bdry, /*neighboring=*/true)
  .for_bit1([&](int par_ix)->bool {
    Box par_box = (*pars.boxes)[par_ix];
    Box z = Box::intersection(par_box, shadow);
    if(!z.is_empty())
      ans.push_back(make_pair(par_ix, z));
    return true;
  });
  
  return ans;
}

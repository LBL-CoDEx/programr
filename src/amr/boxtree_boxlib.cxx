#include "boxtree_boxlib.hxx"
#include "lowlevel/bitops.hxx"
#include "boxmap.hxx"

#include <new>
#include <fstream>
#include <sstream>
#include <string>

using namespace programr;
using namespace programr::amr;
using namespace programr::amr::boxtree;
using namespace std;

namespace {
  const char *bad_file_format = "Badly formatted boxlib input file.";
  
  // at end of file?
  bool at_eof(istream &is) {
    while(true) {
      int c = is.peek();
      if(c == EOF || std::isspace(c)) {
        is.get();
        if(c == EOF)
          return true;
      }
      else
        return false;
    }
  }
  void expect_eof(istream &is) {
    int c;
    do {
      c = is.get();
      USER_ASSERT(c == EOF || std::isspace(c), bad_file_format);
    } while(c != EOF);
  }
  void eat_white(istream &is) {
    while(true) {
      if(std::isspace(is.peek()))
        is.get();
      else
        break;
    }
  }
  
  void expect(istream &is, string lit) {
    istringstream ss(std::move(lit));
    while(!at_eof(ss)) {
      string a, b;
      eat_white(ss); ss >> a;
      eat_white(is); is >> b;
      USER_ASSERT(a == b, bad_file_format);
    }
  }
  
  void expect(istream &is, const char *lit) {
    expect(is, string(lit));
  }
  
  template<class T>
  void expect(istream &is, const T &lit) {
    T x;
    is >> x;
    USER_ASSERT(x == lit, bad_file_format);
  }
  
  template<class T>
  T read(istream &is) {
    T x;
    is >> x;
    return x;
  }
  
  // read a non-empty line, returns empty stream if eof
  void readline(istream &i, istringstream &ss) {
    while(!i.eof()) {
      string s;
      getline(i, s);
      
      bool allw = true;
      for(char c: s)
        allw = allw && std::isspace(c);
      
      if(!allw) {
        ss.~istringstream();
        ::new(&ss) istringstream(s);
        return;
      }
    }
    
    ss.~istringstream();
    ::new(&ss) istringstream(string());
  }
  
  istream& operator>>(istream &i, Pt<int> &x) {
    expect(i, '(');
    i >> x[0];
    expect(i, ',');
    i >> x[1];
    expect(i, ',');
    i >> x[2];
    expect(i, ')');
    return i;
  };
}

tuple<vector<Level>,Box,vector<Ref<BoxMap<int>>>>
boxtree::load_boxlib(const char *file) {
  ifstream f(file);
  USER_ASSERT(f, "file not found");
  istringstream ss;
  
  readline(f, ss);
  expect(ss, "INITIAL GRIDS");
  expect_eof(ss);
  
  readline(f, ss);
  expect(ss," Physical Boundary");
  expect_eof(ss);
  
  readline(f, ss);
  expect(ss, "B:");
  expect(ss, '(');
  Box bdry_box = {read<Pt<int>>(ss), read<Pt<int>>(ss) + Pt<int>(1)};
  expect(ss, Pt<int>(0));
  expect(ss, ')');
  
  expect(ss, bdry_box.size()[0]);
  expect(ss, bdry_box.size()[1]);
  expect(ss, bdry_box.size()[2]);
  
  expect(ss, "::");
  expect(ss, 0);
  expect_eof(ss);
  
  readline(f, ss);
  expect(ss, "Refinement Ratios");
  
  readline(f, ss);
  expect(ss, "R:");
  
  vector<int> refs;
  expect(ss, 1);
  refs.push_back(1);
  while(!at_eof(ss))
    refs.push_back(read<int>(ss));
  
  int lev_n = refs.size();
  vector<Level> tower(lev_n);
  vector<Ref<BoxMap<int>>> rank_maps(lev_n);
  
  int lev = 0;
  int lev_ix0 = 0;
  int cell_scale_log2 = 0;
  // TODO: can read this in from environment
  const bool force_ordered_rank = true;
  int box_id = 0;
  
  while(true) {
    readline(f, ss);
    if(at_eof(ss)) break;
    
    expect(ss, "Level");
    expect(ss, lev);
    int box_n = read<int>(ss);
    expect(ss, "grids");
    
    unique_ptr<Box[]> lev_boxes(new Box[box_n]);
    vector<int> lev_ranks(box_n);
    
    for(int i=0; i < box_n; i++) {
      readline(f, ss);
      expect(ss, lev);
      expect(ss, ':');
      expect(ss, '(');
      Box box = {read<Pt<int>>(ss), read<Pt<int>>(ss) + Pt<int>(1)};
      expect(ss, Pt<int>(0));
      expect(ss, ')');
      for(int d=0; d < 3; d++)
        expect(ss, box.hi[d]-box.lo[d]);
      expect(ss, "::");
      int rank = read<int>(ss);
      expect_eof(ss);
      lev_boxes[i] = box;
      lev_ranks[i] = force_ordered_rank ? box_id++ : rank;
    }
    
    cell_scale_log2 += bitlog2dn(unsigned(refs[lev]));
    
    tower[lev] = Level{
      /*boxes*/new BoxList(box_n, std::move(lev_boxes)),
      /*cell_scale_log2*/cell_scale_log2,
      /*box_scale_log2*/cell_scale_log2
    };
    rank_maps[lev] = BoxMap<int>::make_by_ix(
        tower[lev].boxes,
        [&](int idx) { return lev_ranks[idx]; }
    );
    
    lev += 1;
    lev_ix0 += box_n;
  }

  USER_ASSERT(lev == lev_n, "lev_n mis-match");
  
  return make_tuple(std::move(tower), bdry_box, std::move(rank_maps));
}

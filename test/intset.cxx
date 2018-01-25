#include "lowlevel/intset.hxx"

#include <iostream>

using namespace programr;
using namespace std;

int main() {
  IntSet<int> a;
  for(int i=0; i < 100; i++)
    a.put(i*i);
  for(int i=0; i < 100; i++)
    if(!a.has(i*i))
      cout << "BAD " << i << '\n';
  
  ByteSeqPtr flat = a.as_byteseq().finish(
    [](std::size_t sz) {
      cout << "size=" << sz << '\n';
      return (std::uint8_t*)std::malloc(sz);
    }
  );
  
  unsigned h = 0;
  flat.for_bit1([&](int x)->bool {
    h += x;
    h *= 41;
    //cout << x << '\n';
    return true;
  });
  std::free(flat.ptr);
  cout << "h=" << h << '\n';
  
  IntSet<int> b;
  b |= a;
  for(int i=0; i < 100; i++)
    if(!b.has(i*i))
      cout << "BAD " << i << '\n';
  
  return 0;
}

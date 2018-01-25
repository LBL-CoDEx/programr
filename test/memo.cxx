#include "memo.hxx"

#include <iostream>
#include <string>
#include <unordered_map>

using namespace programr;
using namespace std;

struct Poo: Referent {
  ~Poo() { cout << "poo dies\n";}
};

int main() {
  //MemoTable<int,Ref<Referent>> t([](Referent *r) { cout << "pooputing\n";return 3; });
  Memo<int,Ref<Referent>> t([](Referent *r)->int { cout << "pooputing\n";return 3; });
  {
    Ref<Poo> poo = new Poo;
    cout << "t(poo)="<<t(poo)<<'\n';
    cout << "t(poo)="<<t(poo)<<'\n';
  }
  cout <<"poo should be dead\n";
  
  
  Memo<int,ImmBoxed<int[]>> sum(
    [](const ImmBoxed<int[]> &a)->int {
      cout << "summing " << a.size() << " long.\n";
      int acc = 0;
      for(int i=0; i < (int)a.size(); i++)
        acc += a[i];
      return acc;
    }
  );
  
  ImmBoxed<int[]> x1, x2;
  x1.alloc(5, [](int i, void *p) { ::new(p) int(i); });
  x2.alloc(10, [](int i, void *p) { ::new(p) int(i); });
  cout << "sum(x1)=" << sum(x1) << '\n';
  cout << "sum(x2)=" << sum(x2) << '\n';
  
  ImmBoxed<int[]> y1, y2;
  y1.alloc(5, [](int i, void *p) { ::new(p) int(i); });
  y2.alloc(10, [](int i, void *p) { ::new(p) int(i); });
  cout << "sum(y1)=" << sum(y1) << '\n';
  cout << "sum(y2)=" << sum(y2) << '\n';
  
  return 0;
}

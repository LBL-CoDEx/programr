#include "typeclass.hxx"
#include <iostream>
#include <vector>

using namespace programr;
using namespace std;

struct A {};

bool operator==(A a, A b) { return true; }

template<bool b> struct X;
template<> struct X<true> { int yep; };
template<> struct X<false> {};

template<class T>
struct HasEq {
  template<class U, class = decltype(declval<U>()==declval<U>())>
  static std::true_type test(int);
  template<class U>
  static std::false_type test(...);
  
  static constexpr bool value = decltype(test<T>(666))::value;
};

template<class T>
struct B: X<HasEq<T>::value> {
};

int main() {
  B<X<false>> b;
  cout << HasEq<vector<B<int>>>::value <<'\n';
  return 0;
}

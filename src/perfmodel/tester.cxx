#include <cstdio>
#include <cmath>
#include <array>

#include "perfmodel.hxx"

using namespace std;

int main()
{
  perf::flag_debug = true;
  array<int, 3> tile {128, 64, 32};

  double result = compute_s(perf::resid, tile);
  printf("Tile (%d, %d, %d) residual: %g secs\n\n", tile[0], tile[1], tile[2], result);

  int tsize = cbrt(tile[0]*tile[1]*tile[2]);
  result = compute_s(perf::resid, tsize*tsize*tsize);
  printf("Tile (%d, %d, %d) residual: %g secs\n\n", tsize, tsize, tsize, result);

  result = compute_s(perf::smooth, tile);
  printf("Tile (%d, %d, %d) smooth: %g secs\n\n", tile[0], tile[1], tile[2], result);

  result = compute_s(perf::prolong, tile);
  printf("Tile (%d, %d, %d) prolong: %g secs\n\n", tile[0], tile[1], tile[2], result);

  result = compute_s(perf::restr, tile);
  printf("Tile (%d, %d, %d) restrict: %g secs\n\n", tile[0], tile[1], tile[2], result);

  return 0;
}

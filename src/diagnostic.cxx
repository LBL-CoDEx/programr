#include "diagnostic.hxx"

int programr::Say::indent = 0;

extern "C" void dbgbrk() {
}

void programr::warning(const char *file, int line, const char *msg) {
  std::cerr << "WARNING: " << msg << " (see " << file << ':' << line << ").\n";
}

void programr::user_error(const char *file, int line, const char *msg) {
  std::cerr << "USER ERROR: " << msg << " (see " << file << ':' << line << ").\n";
  dbgbrk();
  std::abort();
}

void programr::dev_error(const char *file, int line, const char *msg) {
  std::cerr << "BUG! See " << file << ':' << line << '.';
  if(msg)
    std::cerr << ' ' << msg;
  std::cerr << '\n';
  dbgbrk();
  std::abort();
}

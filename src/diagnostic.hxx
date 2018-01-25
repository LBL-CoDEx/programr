#ifndef _15534581_29e5_47cd_ac29_e0712447c049
#define _15534581_29e5_47cd_ac29_e0712447c049

# include "knobs.hxx"

# include <cstdlib>
# include <iostream>
# include <sstream>

extern "C" void dbgbrk();

namespace programr {
  void user_error(const char *file, int line, const char *msg);
  void dev_error(const char *file, int line, const char *msg=0x0);
  void warning(const char *file, int line, const char *msg);
  
# define WARNING(msg) \
::programr::warning(__FILE__, __LINE__, msg)\


# define WARNING_F(msg) \
do {\
  ::std::stringstream ss;\
  ss << msg;\
  ::programr::warning(__FILE__, __LINE__, ss.str().c_str());\
} while(0)

# define DEV_ERROR(msg) ::programr::dev_error(__FILE__, __LINE__, msg)

# define USER_ASSERT(ok,msg) \
do {\
  if(!(ok)) ::programr::user_error(__FILE__, __LINE__, msg);\
} while(0)

# define USER_ASSERT_F(ok,msg) \
do {\
  if(!(ok)) {\
    ::std::stringstream ss;\
    ss << msg;\
    ::programr::user_error(__FILE__, __LINE__, ss.str().c_str());\
  }\
} while(0)

#  define HARD_ASSERT(ok) \
do {\
  if(!(ok)) ::programr::dev_error(__FILE__, __LINE__);\
} while(0)

# if KNOB_DEV_ASSERT
#  define DEV_ASSERT(ok) HARD_ASSERT(ok)
# else
#  define DEV_ASSERT(ok)
# endif

# define SAY(yep,msg) \
do { \
  if(yep) Say() << msg; \
} while(0)

  // writes a line to stderr atomically
  // usage:
  //   Say() << "hello" << ' ' << "world"; // ending newline implied
  class Say {
    std::stringstream ss;
  public:
    static int indent;

    Say(int indent_more=0) {
      if(indent_more < 0)
        indent += indent_more;
      //ss << '[' << _::rank_here << "] ";
      for(int i=0; i < 2*indent; i++)
        ss << ' ';
      if(indent_more > 0)
        indent += indent_more;
    }
    ~Say() {
      ss << '\n';
      std::cerr << ss.str();
      std::cerr.flush();
    }
    
    template<class T>
    Say& operator<<(const T &x) {
      ss << x;
      return *this;
    }
  };
  
  template<class T>
  struct PrettyCollection {
    const T &x;
    inline friend std::ostream& operator<<(std::ostream &o, const PrettyCollection<T> &pc) {
      o << "{size=" << pc.x.size() << ':';
      //unsigned i = 0;
      for(const auto &x: pc.x)
        o << /*i++ << ':' << */x << ',';
      o << '}';
      return o;
    }
  };
  
  template<class T>
  inline PrettyCollection<T> pretty(const T &x) {
    return PrettyCollection<T>{x};
  }
}

namespace std {
  template<class A, class B>
  inline ostream& operator<<(ostream &o, const pair<A,B> &ab) {
    return o << '('<<ab.first<<','<<ab.second<<')';
  }
}
#endif


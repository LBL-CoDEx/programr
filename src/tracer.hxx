#ifndef _8561d566_4895_49da_ad03_896fe2461c26
#define _8561d566_4895_49da_ad03_896fe2461c26

# include "expr.hxx"

# include <cstdint>
# include <functional>
# include <string>

namespace programr {
  struct Tracer {
    struct TaskDepTask {
      std::uint64_t task;
      std::size_t bytes;
      Digest<128> digest;
    };
    
    virtual void task(
      std::uint64_t task_id,
      int rank,
      std::uint64_t data_id,
      const std::vector<TaskDepTask> &dep_tasks,
      const std::vector<std::uint64_t> &dep_rdxns,
      std::string note,
      double seconds
    ) = 0;
    
    virtual void reduction(
      std::uint64_t rdxn_id,
      std::size_t bytes,
      const std::vector<std::uint64_t> &dep_tasks,
      const std::vector<std::uint64_t> &dep_rdxns
    ) = 0;
    
    virtual void retire(std::uint64_t data_id) = 0;

    virtual void post_compute_exec() {};
    
    void run(Ref<Expr> root);
  };
  
  struct TracerStdout: Tracer {
    void task(
      std::uint64_t task_id,
      int rank,
      std::uint64_t data_id,
      const std::vector<TaskDepTask> &dep_tasks,
      const std::vector<std::uint64_t> &dep_rdxns,
      std::string note,
      double seconds
    );
    
    void reduction(
      std::uint64_t rdxn_id,
      std::size_t bytes,
      const std::vector<std::uint64_t> &dep_tasks,
      const std::vector<std::uint64_t> &dep_rdxns
    );
    
    void retire(std::uint64_t data_id);
  };
}

#endif

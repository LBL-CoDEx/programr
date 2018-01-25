#ifndef _a8ce8d36_29d0_4561_8a3f_3002da43f18b
#define _a8ce8d36_29d0_4561_8a3f_3002da43f18b

# include "tracer.hxx"
# include "lowlevel/intset.hxx"
# include "lowlevel/digest.hxx"

# include <unordered_map>
# include <unordered_set>
# include <string>
# include <vector>

namespace {
  const bool flag_verbose_tracer = false;
  inline unsigned flip_low(unsigned x) {
    return ~(~x + 1) & x; // flips least significant 1 bit
  }
}

namespace programr {
  class TracerGraph: public Tracer {
    struct Data {
      std::unordered_set<Digest<128>> comms;
      IntSet<std::uint64_t> tasks;
    };
    struct Task {
      int rank;
    };
    std::unordered_map<std::uint64_t,Data> _datas;
    std::unordered_map<std::uint64_t,Task> _tasks;

    // captured metadata
    std::unordered_map<int, double> comps;
    std::unordered_map<std::pair<int, int>, std::pair<int, size_t>> comms;

    void add_comp(int node_id, double secs, std::string note) {
      if (flag_verbose_tracer) std::cout << "comp: (" << note << ", " << node_id << ", " << secs << ")" << std::endl;
      comps[node_id] += secs;
    }

    void add_comm(int src, int dst, size_t bytes) {
      if (flag_verbose_tracer) std::cout << "comm: (" << src << ", " << dst << ", " << bytes << ")" << std::endl;
      auto &entry = comms[{src, dst}];
      entry.first += 1;
      entry.second += bytes;
    }

    void add_coll(IntSet<int> teamnodes, size_t bytes) {
      if (flag_verbose_tracer) std::cout << "coll: (" << teamnodes << ", " << bytes << ")" << std::endl;
      std::vector<int> vec; {
        teamnodes.for_each([&](int x) {vec.push_back(x);});
        if (flag_verbose_tracer) sort(vec.begin(), vec.end());
      }
      for (unsigned i = 1; i < vec.size(); ++i) {
        int src = vec[i];
        int dst = vec[flip_low(i)];
        if (flag_verbose_tracer) std::cout << "coll msg: (" << src << ", " << dst << ")" << std::endl;
        auto &entry1 = comms[{src, dst}]; // up the tree
        entry1.first += 1;
        entry1.second += bytes;
        auto &entry2 = comms[{dst, src}]; // down the tree
        entry2.first += 1;
        entry2.second += bytes;
      }
    }

  public:
    TracerGraph() {}

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

    const std::unordered_map<int, double> & get_comps() const { return comps; }
    const std::unordered_map<std::pair<int, int>, std::pair<int, size_t>> & get_comms() const { return comms; }
  };
}
#endif

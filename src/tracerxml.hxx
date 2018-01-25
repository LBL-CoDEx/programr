#ifndef _3126fdfc_a126_464d_8960_b6d102baedcc
#define _3126fdfc_a126_464d_8960_b6d102baedcc

# include "tracer.hxx"
# include "lowlevel/intset.hxx"
# include "lowlevel/digest.hxx"

# include <unordered_map>
# include <unordered_set>

namespace programr {
  class TracerXml: public Tracer {
    struct Data {
      std::unordered_map<Digest<128>,std::uint64_t> comms;
      IntSet<std::uint64_t> tasks;
    };
    struct Task {
      int rank;
      std::string note;
    };
    int _rank_n;
    std::uint64_t _comm_id_next;
    std::unordered_map<std::uint64_t,Data> _datas;
    std::unordered_map<std::uint64_t,Task> _tasks;
    std::ostream *_file;
    bool _flag_totals;
    const std::string _totals_file  = "comm_totals.tsv";
    std::unordered_map<std::pair<int, int>, size_t> _totals;
    std::uint64_t _comp_epoch;
  
  public:
    TracerXml(int rank_n, std::ostream *file);
    ~TracerXml();
    
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

    void post_compute_exec() override;

  private:
# if KNOB_XML_VERIFY
    struct Event {
      std::size_t dep_n;
      std::vector<std::uint64_t> sats;
    };
    
    std::unordered_map<std::uint64_t, Event> _events;
    std::unordered_set<std::uint64_t> _ready;
    bool _ok_id_reused = true;
    bool _ok_dep_not_defined = true;
    bool _ok_dep_redundant = true;
# endif    
    
    void _log_comm(int src, int dst, size_t byte_n) {
      _totals[{src,dst}] += byte_n;
    }
    void _dump_totals();
    void _event_define(std::uint64_t id, std::vector<uint64_t> &&deps);
    
  public:
    bool verify();
  };
}
#endif

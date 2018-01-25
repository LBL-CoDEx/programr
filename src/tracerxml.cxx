#include "tracerxml.hxx"
#include "lowlevel/spookyhash.hxx"
#include "env.hxx"

#include <sstream>
#include <fstream>

using namespace programr;
using namespace std;

namespace {
  template<class T>
  void uniquify(vector<T> &v) {
    std::sort(v.begin(), v.end());
    auto last = std::unique(v.begin(), v.end());
    v.erase(last, v.end());
  }

  // keep track of which ranks participate in which reductions
  unordered_map<std::uint64_t, unordered_set<std::uint64_t>> rdxn_teams;
  unordered_map<std::uint64_t, vector<std::uint64_t>> rdxn_team_vecs;
  void add_to_rdxn_team(std::uint64_t rdxn_id, std::uint64_t rank) {
    rdxn_teams[rdxn_id].insert(rank); // create entry if needed
  }
  bool rank_in_rdxn_team(std::uint64_t rdxn_id, std::uint64_t rank) {
    return rdxn_teams.at(rdxn_id).count(rank) > 0;
  }
  std::uint64_t rand_team_rank(std::uint64_t rdxn_id) {
    if (rdxn_team_vecs.count(rdxn_id) == 0) {
      for (auto rank_id : rdxn_teams.at(rdxn_id)) {
        rdxn_team_vecs[rdxn_id].push_back(rank_id);
      }
    }
    // low quality rand is okay
    int rand_idx = rand() % rdxn_team_vecs[rdxn_id].size();
    return rdxn_team_vecs[rdxn_id][rand_idx];
  }
}

TracerXml::TracerXml(int rank_n, std::ostream *file):
  _rank_n(rank_n),
  _file(file) {
  
  _comm_id_next = 0;
  *_file << "<events>\n";
  _flag_totals = env<bool>("commtotals", false);
  _comp_epoch = 0;
}

TracerXml::~TracerXml() {
  *_file << "</events>\n";
  if (_flag_totals) _dump_totals();
}

void TracerXml::task(
    std::uint64_t task_id,
    int rank_id,
    std::uint64_t data_id,
    const std::vector<TaskDepTask> &dep_tasks,
    const std::vector<std::uint64_t> &dep_rdxns,
    std::string note,
    double seconds
  ) {
  
  Task &task = _tasks[task_id];
  task.rank = rank_id;
  task.note = std::move(note);
  
  Data &data = _datas[data_id];
  data.tasks.put(task_id);
  
  stringstream depstr;
  bool first_dep = true;
  
  uint64_t event_id = 0 + 3*task_id;
  vector<uint64_t> dep_event_ids;
  
  unordered_set<uint64_t> rdxns_left;
  for(auto rdxn_id: dep_rdxns) {
    if (rank_in_rdxn_team(rdxn_id, rank_id)) {
      if(!first_dep) depstr << ',';
      first_dep = false;
      depstr << "e" << 2 + 3*rdxn_id;
      
      dep_event_ids.push_back(2 + 3*rdxn_id);
    } else {
      rdxns_left.insert(rdxn_id);
    }
  }
  
  for(TaskDepTask dep: dep_tasks) {
    int rank_d = rank_id;
    int rank_s = _tasks[dep.task].rank;
    
    SpookyHasher h({(uint64_t)rank_d, dep.task});
    h.consume(dep.digest);
    Digest<128> comm = h.digest();
    
    uint64_t task_dep_id;
    
    if(data.comms.count(comm) == 0) {
      if(!(KNOB_XML_SELF_COMMS) && rank_d == rank_s) {
        task_dep_id = 0 + 3*dep.task;
      }
      else {
        uint64_t comm_id = _comm_id_next++;
        data.comms[comm] = comm_id;
        task_dep_id = 1 + 3*comm_id;

        vector<uint64_t> comm_dep_event_ids;
        stringstream comm_depstr;

        // add task dependency
        uint64_t comm_dep_id = 0 + 3*dep.task;
        comm_dep_event_ids.push_back(comm_dep_id);
        comm_depstr << "e" << comm_dep_id;

        // add dependency to task's reductions if rank_s is a team member
        // FIXME: this isn't quite right for comms that are reused by
        //        multiple tasks with differing reduction dependencies.
        for (auto rdxn_id : dep_rdxns) {
          if (rank_in_rdxn_team(rdxn_id, rank_s)) {
            comm_dep_id = 2 + 3*rdxn_id;
            comm_dep_event_ids.push_back(comm_dep_id);
            comm_depstr << ",e" << comm_dep_id;
            rdxns_left.erase(rdxn_id);
          }
        }
        
        *_file << "<comm "
          "id=\"e" << task_dep_id << "\" "
          "dep=\"" << comm_depstr.str() << "\" "
          "from=\"" << rank_s << "\" "
          "to=\"" << rank_d << "\" "
          "size=\"" << dep.bytes << "\" "
          "epoch=\"" << _comp_epoch << "\" "
          "/>\n";
        
        _event_define(task_dep_id, std::move(comm_dep_event_ids));
        if (_flag_totals) _log_comm(rank_s, rank_d, dep.bytes);
      }
    }
    else
      task_dep_id = 1 + 3*data.comms[comm];
    
    if(!first_dep) depstr << ',';
    first_dep = false;
    depstr << "e" << task_dep_id;
    
    dep_event_ids.push_back(task_dep_id);
  }

  for (auto rdxn_id : rdxns_left) {
    // send size 0 control message from a team member to here
    uint64_t comm_id = _comm_id_next++;
    uint64_t task_dep_id = 1 + 3*comm_id;
    int rank_s = rand_team_rank(rdxn_id);
    int rank_d = rank_id;

    // control message depends on reduction
    uint64_t comm_dep_id = 2 + 3*rdxn_id;
    stringstream comm_depstr;
    comm_depstr << "e" << comm_dep_id;

    *_file << "<comm "
      "id=\"e" << task_dep_id << "\" "
      "dep=\"" << comm_depstr.str() << "\" "
      "from=\"" << rank_s << "\" "
      "to=\"" << rank_d << "\" "
      "size=\"" << 0 << "\" "
      "epoch=\"" << _comp_epoch << "\" "
      "/>\n";
    
    _event_define(task_dep_id, {comm_dep_id});

    // task depends on control message
    if(!first_dep) depstr << ',';
    first_dep = false;
    depstr << "e" << task_dep_id;
    dep_event_ids.push_back(task_dep_id);
  }
  
  *_file << "<comp "
    "id=\"e" << event_id << "\" "
    "dep=\"" << depstr.str() << "\" "
    "at=\"" << rank_id << "\" "
    //"size=\"" << "?" << "\" "
    "time=\"" << seconds << "\" "
    "epoch=\"" << _comp_epoch+1 << "\" "
#if KNOB_XML_NOTE
    "note=\"" << task.note << "\" "
#endif
    "/>\n";
  
  _event_define(event_id, std::move(dep_event_ids));
}

void TracerXml::reduction(
    std::uint64_t rdxn_id,
    std::size_t bytes,
    const std::vector<std::uint64_t> &dep_tasks,
    const std::vector<std::uint64_t> &dep_rdxns
  ) {
  stringstream depstr;
  bool first_dep = true;
  
  vector<uint64_t> dep_event_ids;
  
  IntSet<int> teamset;
  stringstream teamstr;
  bool first_team = true;
  
  for(std::uint64_t task_id: dep_tasks) {
    if(!first_dep) depstr << ',';
    first_dep = false;
    depstr << "e" << 0 + 3*task_id;
    
    dep_event_ids.push_back(0 + 3*task_id);
    
    if(!teamset.put(_tasks[task_id].rank)) {
      if(!first_team) teamstr << ',';
      first_team = false;
      teamstr << _tasks[task_id].rank;
      add_to_rdxn_team(rdxn_id, _tasks[task_id].rank);
    }
  }
  
  for(std::uint64_t dep_rdxn_id: dep_rdxns) {
    //Say() << "collective dependency: " << 2+3*rdxn_id << " depends on " << 2+3*dep_rdxn_id;
    if(!first_dep) depstr << ',';
    first_dep = false;
    depstr << "e" << 2 + 3*dep_rdxn_id;
    
    dep_event_ids.push_back(2 + 3*dep_rdxn_id);
  }
  
  uint64_t event_id = 2 + 3*rdxn_id;
  *_file << "<coll "
    "id=\"e" << event_id << "\" "
    "dep=\"" << depstr.str() << "\" "
    "type=\"ALLREDUCE\" "
    "team=\"" << teamstr.str() << "\" "
    "size=\"" << bytes << "\" "
    "epoch=\"" << _comp_epoch << "\" "
    "/>\n";
  
  _event_define(event_id, std::move(dep_event_ids));
}
    
void TracerXml::retire(std::uint64_t data_id) {
  Data &data = _datas[data_id];
  data.tasks.for_each([&](std::uint64_t task_id) {
    _tasks.erase(task_id);
  });
  _datas.erase(data_id);
}

void TracerXml::post_compute_exec() { ++_comp_epoch; }

void TracerXml::_dump_totals() {
  // determine max rank
  int max_rank = 0;
  for (const auto &p : _totals) {
    int src, dst;
    tie(src,dst) = p.first;
    max_rank = std::max({max_rank, src, dst});
  }

  ofstream ofs;
  ostream *os;
  if (_totals_file == "-") {
    os = &std::cout;
  } else {
    ofs.open(_totals_file);
    os = &ofs;
  }

  // print table
  for (int dst = 0; dst <= max_rank; ++dst) {
    *os << "\t" << dst;
  }
  *os << endl;
  for (int src = 0; src <= max_rank; ++src) {
    *os << src;
    for (int dst = 0; dst <= max_rank; ++dst) {
      size_t byte_n = 0;
      try {
        byte_n = _totals.at({src,dst});
      } catch (const std::out_of_range &e) {}
      *os << "\t" << byte_n;
    }
    *os << endl;
  }
}

#if KNOB_XML_VERIFY

void TracerXml::_event_define(uint64_t id, vector<uint64_t> &&deps) {
  size_t dep_n_old = deps.size();
  uniquify(deps);
  if(deps.size() != dep_n_old)
    _ok_dep_redundant = false;
  
  if(_events.count(id) != 0)
    _ok_id_reused = false;
  
  for(uint64_t dep: deps) {
    if(_events.count(dep) == 0) {
      //cerr << "dep_not_defined id="<<id<<" dep="<<dep<<'\n';
      _ok_dep_not_defined = false;
    }
    
    _events[dep].sats.push_back(id);
  }
  
  _events[id] = Event{deps.size(), {}};
  if(deps.size() == 0)
    _ready.insert(id);
}

bool TracerXml::verify() {
  size_t exec_n = 0;
  
  while(_ready.size() != 0) {
    uint64_t id = *_ready.begin();
    _ready.erase(_ready.begin());
    
    for(uint64_t sat: _events[id].sats) {
      if(0 == --_events[sat].dep_n)
        _ready.insert(sat);
    }
    
    exec_n += 1;
  }
  
  bool ok = true;
  
  if(!_ok_dep_redundant)
    cerr << "VERIFY FAILED: dependency list contains duplicate ids.\n";
  ok = ok && _ok_dep_redundant;
  
  if(!_ok_id_reused)
    cerr << "VERIFY FAILED: id reused.\n";
  ok = ok && _ok_id_reused;
  
  if(!_ok_dep_not_defined)
    cerr << "VERIFY FAILED: dependency id not previosuly defined.\n";
  ok = ok && _ok_dep_not_defined;
  
  if(exec_n != _events.size()) {
    cerr << "VERIFY FAILED: cycle detected!\n";
    ok = false;
  }
  
  if(ok)
    cerr << "VERIFY SUCCESS\n";
  
  return ok;
}

#else

void TracerXml::_event_define(uint64_t id, vector<uint64_t> &&deps) {}
bool TracerXml::verify() { return true; }

#endif

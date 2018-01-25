#include "tracergraph.hxx"
#include "lowlevel/spookyhash.hxx"

#include <sstream>

using namespace programr;
using namespace std;

void TracerGraph::task(
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
  
  Data &data = _datas[data_id];
  data.tasks.put(task_id);
  
  for(TaskDepTask dep: dep_tasks) {
    int rank_d = rank_id;
    int rank_s = _tasks[dep.task].rank;
    
    SpookyHasher h({(uint64_t)rank_d, dep.task});
    h.consume(dep.digest);
    Digest<128> comm = h.digest();
    
    if(data.comms.count(comm) == 0 && rank_s != rank_d) {
      data.comms.insert(comm);
      add_comm(rank_s, rank_d, dep.bytes);
    }
  }
  
  add_comp(rank_id, seconds, note);
}

void TracerGraph::reduction(
    std::uint64_t rdxn_id,
    std::size_t bytes,
    const std::vector<std::uint64_t> &dep_tasks,
    const std::vector<std::uint64_t> &dep_rdxns
  ) {
  IntSet<int> teamranks;
  for(std::uint64_t task_id: dep_tasks) {
    teamranks.put(_tasks[task_id].rank);
  }
  add_coll(teamranks, bytes);
}
    
void TracerGraph::retire(std::uint64_t data_id) {
  Data &data = _datas[data_id];
  data.tasks.for_each([&](std::uint64_t task_id) {
    _tasks.erase(task_id);
  });
  _datas.erase(data_id);
}

#include "app.hxx"
#include "env.hxx"
#include "tracerxml.hxx"
#include "tracergraph.hxx"
#include "amr/boxtree_boxlib.hxx"

#ifdef KNOB_MOTA
#include "amr/mota/mota.hxx"
#endif

#include <fstream>
#include <tuple>
#include <string>
#include <chrono>

using namespace std;
using namespace programr;
using namespace programr::amr;

#ifdef KNOB_MOTA
using namespace mota;
#endif

namespace {
  bool flag_periodic_bdry = env<bool>("periodic", true);
  bool flag_skip_existing = env<bool>("skipexist", false);

  tuple<Ref<Boundary>, IList<LevelAndRanks>>
  make_mesh() {
    vector<boxtree::Level> levels;
    Box dom;
    tie(levels,dom) = boxtree::make_octree_full(3, 8);
    //tie(levels,dom) = boxtree::make_octree_full(6, 8);
    //tie(levels,dom) = boxtree::make_telescope(3, 2, 8);
    
    vector<LevelAndRanks> tree;
    for(int i=0; i < (int)levels.size(); i++) {
      tree.push_back(LevelAndRanks{
        levels[i],
        BoxMap<int>::make_by_ix(
          levels[i].boxes,
          [&](int ix) { return ix; }
        )
      });
    }

    return make_tuple(
      flag_periodic_bdry ? Ref<Boundary>(new BoundaryPeriodic(dom))
                         : Ref<Boundary>(new BoundarySimple(dom)),
      List<LevelAndRanks>::make(tree)
    );
  }

  tuple<Ref<Boundary>, IList<LevelAndRanks>>
  load_mesh(const char *file) {
    vector<boxtree::Level> levels;
    Box dom;
    vector<Ref<BoxMap<int>>> maps;
    tie(levels,dom,maps) = boxtree::load_boxlib(file);
    
    vector<LevelAndRanks> tree;
    for(int i=0; i < (int)levels.size(); i++) {
      tree.push_back(LevelAndRanks{ levels[i], maps[i] });
    }
    
    return make_tuple(
      flag_periodic_bdry ? Ref<Boundary>(new BoundaryPeriodic(dom))
                         : Ref<Boundary>(new BoundarySimple(dom)),
      List<LevelAndRanks>::make(tree)
    );
  }

  const bool flag_debug = false;

  inline bool file_exists(const string &filename) {
    ifstream infile(filename);
    return infile.good();
  }

#ifdef KNOB_MOTA
  void add_geom(shared_ptr<AppGraph_> app_g, IList<LevelAndRanks> tree) {
    int center_scale_log2 = numeric_limits<int>::min();
    tree->for_val([&](const LevelAndRanks &lr) {
      // centers need more resolution relative to finest level: (lo+hi)/2
      center_scale_log2 = std::max(center_scale_log2, lr.level.box_scale_log2+1);
    });
    tree->for_ix_val([&](int lev_ix, const LevelAndRanks &lr) {
      auto lev = lr.level;
      auto ranks = lr.rank_map;
      auto boxes = lev.boxes;
      if (flag_debug) { Say() << "Level " << lev_ix; }
      Imm<BoxMap<Pt<int>>> centers = lev.geom_centers(center_scale_log2);
      for (int i = 0; i < boxes->size(); ++i) {
        const Box &b = (*boxes)[i];
        int node_id = (*ranks)(b); // use rank in rank_map as id
        Pt<int> center = (*centers)(b);
        if (flag_debug) {
          Pt<double> scaled = Pt<double>(center) / (1 << center_scale_log2);
          Say() << "Box " << b << ": (rank, center) = (" << node_id << ", " << scaled << ")";
        }
        app_g->set_node_loc(node_id, array<int,3>{center[0], center[1], center[2]});
        app_g->set_node_lev(node_id, lev_ix);
        app_g->set_node_cell_n(node_id, b.elmt_n() >> (3*lev.unit_per_cell_log2()));
      }
    });
  }

  tuple<unordered_map<int, double>,            // comps
        unordered_map<pair<int, int>, pair<int, size_t>>> // comms
  estimate_app_g(Ref<Boundary> bdry, IList<LevelAndRanks> tree,
                 int halo_n, int phalo_n) {
    unordered_map<int, double> comps;
    unordered_map<pair<int, int>, pair<int, size_t>> comms;
    size_t elmt_sz = 8;    // bytes per element
    double comp_fac = 1e-9, // multiplicative factors
           halo0_fac = 15.0, // within-level halo exchange
           halo1_fac = 1.0,  // parent-prolonged halo fill
           rest_fac = 1.0,
           prol_fac = 1.0;

    tree->for_ix_val([&](unsigned lev_ix, const LevelAndRanks &lr) {

      if (flag_debug) { Say() << "Level " << lev_ix; }

      // get level, ranks, and boxes for this level, parent, and child
      const boxtree::Level &lev = lr.level;
      auto ranks = lr.rank_map;
      auto boxes = lev.boxes;
      const boxtree::Level *parent = (lev_ix == 0 ? nullptr : &(*tree)[lev_ix-1].level);
      Imm<BoxMap<int>>      pranks = (lev_ix == 0 ? nullptr : (*tree)[lev_ix-1].rank_map);
      Imm<BoxList>          pboxes = (lev_ix == 0 ? nullptr : parent->boxes);
      const boxtree::Level *child  = (lev_ix+1 == tree->size() ? nullptr : &(*tree)[lev_ix+1].level);
      Imm<BoxMap<int>>      cranks = (lev_ix+1 == tree->size() ? nullptr : (*tree)[lev_ix+1].rank_map);
      Imm<BoxList>          cboxes = (lev_ix+1 == tree->size() ? nullptr : child->boxes);

      for (int ix = 0; ix < boxes->size(); ++ix) {
        const Box &b = (*boxes)[ix];
        int node_id = (*ranks)(b); // use rank in rank_map as id
        if (flag_debug) { Say() << "Box " << node_id << ": " << b; }
        // compute
        comps[node_id] += comp_fac * b.elmt_n();
        // halo deps
        for (const auto &dep : boxtree::deps_halo(lev, parent, ix, halo_n, bdry, phalo_n)) {
          int dep_lev, dep_ix; Box dep_box;
          tie(dep_lev, dep_ix, dep_box) = dep;
          int dep_id = dep_lev == 0 ? ( *ranks)(( *boxes)[dep_ix])
                                    : (*pranks)((*pboxes)[dep_ix]);
          int scale_log2 = dep_lev == 0 ? lev.unit_per_cell_log2()
                                        : parent->unit_per_cell_log2();
          int halo_fac = dep_lev == 0 ? halo0_fac : halo1_fac;
          size_t byte_n = elmt_sz * (dep_box.elmt_n() >> (3*scale_log2));
          if (flag_debug) { Say() << "  Halo Dep " << dep_id << ": " << dep_box << ": " << byte_n; }
          auto &entry = comms[make_pair(dep_id, node_id)];
          entry.first += 1;
          entry.second += halo_fac * byte_n;
        }
        // restrict deps
        if (child) {
          for (const auto &dep : boxtree::deps_restrict(*child, lev, ix)) {
            int dep_ix; Box dep_box;
            tie(dep_ix, dep_box) = dep;
            int dep_id = (*cranks)((*cboxes)[dep_ix]);
            int scale_log2 = child->unit_per_cell_log2() +
                             (child->cell_scale_log2 - lev.cell_scale_log2);
            size_t byte_n = elmt_sz * ((dep_box.elmt_n() >> 3*scale_log2) +
                                       (dep_box.bdry_face_n() >> 2*scale_log2));
            if (flag_debug) { Say() << "  Rest Dep " << dep_id << ": " << dep_box << ": " << byte_n; }
            auto &entry = comms[make_pair(dep_id, node_id)];
            entry.first += 1;
            entry.second += rest_fac * byte_n;
          }
        }
        // prolong deps
        if (parent) {
          for (const auto &dep : boxtree::deps_prolong(lev, *parent, ix, bdry, phalo_n)) {
            int dep_ix; Box dep_box;
            tie(dep_ix, dep_box) = dep;
            int dep_id = (*pranks)((*pboxes)[dep_ix]);
            int scale_log2 = parent->unit_per_cell_log2();
            size_t byte_n = elmt_sz * (dep_box.elmt_n() >> (3*scale_log2));
            if (flag_debug) { Say() << "  Prol Dep " << dep_id << ": " << dep_box << ": " << byte_n; }
            auto &entry = comms[make_pair(dep_id, node_id)];
            entry.first += 1;
            entry.second += prol_fac * byte_n;
          }
        }
        // TODO: allreduce
      }
    });

    return make_tuple(comps, comms);
  }

  string mapping_filename(const string &topo_str, const string &mapper_str, size_t rank_n) {
    string outdir = env<string>("outdir", "output");
    string rank_n_str; {
      ostringstream os;
      os << rank_n;
      rank_n_str = os.str();
    }
    return outdir +"/"+ topo_str +"."+ mapper_str +"."+ rank_n_str +".xml";
  }

  void run_mapper(string mapper_str, shared_ptr<AppGraph_> app_g,
                                     shared_ptr<NetworkGraph_> net_g,
                  const vector<shared_ptr<AppGraph_>> &eval_apps) {
    string train_statsfile = "mapper_stats_train.tsv";
    string  test_statsfile = "mapper_stats_test.tsv";
    size_t rank_n = net_g->rank_n();

    string mapfile = mapping_filename(net_g->label(), mapper_str, rank_n);
    if (flag_skip_existing && file_exists(mapfile)) {
      Say() << "Output file " << mapfile << " already exists: skipping!";
    } else {
      if (file_exists(mapfile)) {
        Say() << "Output file " << mapfile << " already exists: overwriting!";
      }
      // run mapping requested and write results to disk
      Say() << "\nMapping " << mapfile << " ...";
      auto t0 = std::chrono::high_resolution_clock::now();
      app_g->run_mapper(mapper_str, net_g);
      auto t1 = std::chrono::high_resolution_clock::now();
      auto map_ms = std::chrono::duration_cast<std::chrono::milliseconds>(t1-t0).count();
      Say() << "Mapping took " << map_ms << " ms";
      net_g->print_stats();
      app_g->write_mapping(mapfile);
      net_g->append_stats(train_statsfile, mapper_str, map_ms);

      // do evaluation on test apps (different from training app)
      for (auto eval_app_g : eval_apps) {
        eval_app_g->evaluate_mapping(net_g, app_g->get_mapping());
        net_g->append_stats(test_statsfile, mapper_str, map_ms);
      }
    }
  }

  void run_all_mappers(shared_ptr<AppGraph_> app_g,
                       shared_ptr<NetworkGraph_> net_g,
                       const vector<shared_ptr<AppGraph_>> &eval_apps) {
    for (auto s : app_g->available_mappers()) {
      run_mapper(s, app_g, net_g, eval_apps);
    }
  }

  int run_mota_mappers(Ref<Boundary> bdry, IList<LevelAndRanks> tree) {
    shared_ptr<AppGraph_> est_app_g, sim_app_g, map_app_g;
    vector<shared_ptr<AppGraph_>> eval_apps {};

    // TODO: the mapper code below assumes the rank assigned to the box is a unique box ID,
    //       which is currently guaranteed by load_boxlib's force_ordered_rank flag

    auto amr_level_n = tree->size();
    est_app_g = app_graph_create(amr_level_n);
    sim_app_g = app_graph_create(amr_level_n);
    add_geom(est_app_g, tree); // uses box's rank as node id
    add_geom(sim_app_g, tree); // uses box's rank as node id

    { // get node (compute) and edge (communicate) weights for the app graph
      unordered_map<int, double> comps;
      unordered_map<pair<int, int>, pair<int, size_t>> comms;

      // run tracer and capture events in graph
      Say() << "Running tracer to capture comm events ...";
      TracerGraph tr {};
      tr.run(main_ex(bdry, tree));
      print_slab_counters();
      tie(comps, comms) = tie(tr.get_comps(), tr.get_comms());
      sim_app_g->import(comps, comms);
      sim_app_g->print_stats();

      bool flag_est_app_g = env<bool>("est_appg", false);
      if (flag_est_app_g) {
        int  halo_n = env<int>("est_halo" ,1);
        int phalo_n = env<int>("est_phalo",1);
        Say() << "Estimating comps and comms from tree "
                    << "(halo=" << halo_n << ", phalo=" << phalo_n << ") ...";
        tie(comps, comms) = estimate_app_g(bdry, tree, halo_n, phalo_n);
        est_app_g->import(comps, comms);
        est_app_g->print_stats();
        // map based on estimate, and test based on simulation
        map_app_g = est_app_g;
        eval_apps.push_back(sim_app_g);
      } else {
        // map based on simulation
        map_app_g = sim_app_g;
      }
    }

    size_t box_n = 0; 
    tree->for_val([&](const LevelAndRanks &lr) { box_n += lr.level.boxes->size(); });

    for (string filename : vector<string> {"mapper_stats_train.tsv",
                                           "mapper_stats_test.tsv"}) {
      NetworkStats::write_stats_header(filename);
    }

    auto node_rank_n = env<int>("node_rank_n", 1);
    const size_t min_rank_n = env<size_t>("min_rank_n", 2*node_rank_n);
    const size_t max_rank_n = env<size_t>("max_rank_n", box_n);
    const int step = env<int>("rank_step", 2);
    auto flag_rand_place = env<bool>("rand_place", false);

    std::vector<std::string> net_labels {
      "3dt-edi", "3dt-exa",
      "dfly-edi", "dfly-exa"
    };
    for (const auto &net_label : net_labels) {
      for (size_t rank_n = min_rank_n; rank_n <= max_rank_n; rank_n *= step) {
        auto net_g = net_graph_create(net_label, amr_level_n, rank_n, node_rank_n, flag_rand_place);
        run_all_mappers(map_app_g, net_g, eval_apps);
      }
    }

    return 0;
  }
#endif
}

int main(int arg_n, char **args) {

  Ref<Boundary> bdry;
  IList<LevelAndRanks> tree;
  
  if(arg_n > 1)
    tie(bdry,tree) = load_mesh(args[1]);
  else
    tie(bdry,tree) = make_mesh();

  int result = 0;

  if (env<bool>("events", false)) {
    string outdir = env<string>("outdir", "output");
    string outfile = env<string>("outfile", "events.xml");
    outfile = outdir + "/" + outfile;
    if (flag_skip_existing && file_exists(outfile)) {
      Say() << "Output file " << outfile << " already exists: skipping!";
    } else {
      if (file_exists(outfile)) {
        Say() << "Output file " << outfile << " already exists: overwriting!";
      }
      ofstream o(outfile);
      USER_ASSERT(o, (string("Could not open file: ") + outfile).c_str());
      TracerXml tr(1, &o);

      Say() << "Running XML tracer to generate " << outfile << " ...";
      tr.run(main_ex(bdry, tree));

      result = tr.verify() ? 0 : 1;
    }
  }

#ifdef KNOB_MOTA
  if (env<bool>("mapper", false)) {
    result = run_mota_mappers(bdry, tree);
  }
#endif
  
  return result;
}

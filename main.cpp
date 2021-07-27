//
// Created by chaowyc on 3/3/2021.
//

#include <iostream>
#include <cstring>
#include <ctime>
#include <ratio>
#include <chrono>
#include <iomanip>
#include <csignal>
#include <unistd.h>

#include "Graph.h"
#include "GraphUtil.h"
#include "Grail.h"
#include "ReachBackbone.h"
#include "PathTree.h"
#include "Query.h"
#include "PathtreeQuery.h"
#include "ProgressBar.h"
#include "Tabulation.h"

static int query_num = 100;
static int grail_dim = 2;
static string query_file;
static string graph_file;
static bool gen_query = false;
static bool read_query = false;
static int bb_epsilon = 10;
static bool transitive_closure = false;
static bool reps_tab_alg = false;
static string indexing;

static bool timeout = false;

static void alarm_handler(int param) {
    timeout = true;
}

static void usage() {
    cout << "\nUsage:\n"
            "	csr [-h] [-t] [-m pathtree_or_grail] [-n num_query] [-q query_file] [-g query_file] graph_file\n"
            "Description:\n"
            "	-h\tPrint the help message.\n"
            "	-n\t# reachable queries and # unreachable queries to be generated, 100 for each by default.\n"
            "	-g\tSave the randomly generated queries into file.\n"
            "	-q\tRead the randomly generated queries from file.\n"
            "	-t\tEvaluate transitive closure.\n"
            "	-r\tEvaluate rep's tabulation algorithm.\n"
            "	-m\tEvaluate what indexing approach, pathtree, grail, or pathtree+grail.\n"
            "	-d\tSet the dim of Grail, 2 by default.\n"
         << endl;
}

static void parse_arg(int argc, char *argv[]) {
    if (argc == 1) {
        usage();
        exit(0);
    }
    int i = 1;
    while (i < argc) {
        if (strcmp("-h", argv[i]) == 0) {
            usage();
            exit(0);
        }
        if (strcmp("-n", argv[i]) == 0) {
            i++;
            query_num = atoi(argv[i++]);
        } else if (strcmp("-d", argv[i]) == 0) {
            i++;
            grail_dim = atoi(argv[i++]);
        } else if (strcmp("-g", argv[i]) == 0) {
            i++;
            gen_query = true;
            query_file = argv[i++];
        } else if (strcmp("-q", argv[i]) == 0) {
            i++;
            read_query = true;
            query_file = argv[i++];
        } else if (strcmp("-e", argv[i]) == 0) {
            i++;
            bb_epsilon = atoi(argv[i++]);
        } else if (strcmp("-t", argv[i]) == 0) {
            i++;
            transitive_closure = true;
        } else if (strcmp("-r", argv[i]) == 0) {
            i++;
            reps_tab_alg = true;
        } else if (strcmp("-m", argv[i]) == 0) {
            i++;
            indexing = argv[i++];
        } else if (strcmp("-d", argv[i]) == 0) {
            i++;
            grail_dim = atoi(argv[i++]);
        } else {
            graph_file = argv[i++];
        }
    }

    assert((!gen_query || !read_query) && "Do not use -g and -q together!");
    assert(indexing.empty() || indexing == "pathtree" || indexing == "grail" || indexing == "pathtree+grail");
    if (indexing.empty())
        indexing = "grail";
}

template<typename Src, typename Target>
static double test_query(AbstractQuery *aq, vector<std::pair<int, int>> &queries, bool r, Src src, Target trg) {
    signal(SIGALRM, alarm_handler);
    timeout = false;
    alarm(3600 * 6);

    int succ_num = 0;
    auto start = std::chrono::high_resolution_clock::now();
    for (const auto &rs : queries) {
        int s = src(rs.first);
        int t = trg(rs.second);
        aq->reset();
        bool pt_r = aq->reach(s, t);
        if (pt_r != r) {
            cerr << "### Wrong: [" << rs.first << "] to [" << rs.second << "] reach = " << pt_r << endl;
        } else {
            succ_num++;
        }
        if (timeout)
            break;
    }
    auto end = std::chrono::high_resolution_clock::now();
    chrono::duration<double, std::milli> diff = end - start;
    double query_time = diff.count();

    cout << aq->method() << " for " << queries.size();
    if (r)
        cout << " reachable queries: ";
    else
        cout << " unreachable queries: ";
    cout << (int) query_time << " ms. Success rate: " << (succ_num / queries.size()) * 100 << " %." << endl;
    return query_time;
}

static void read_or_generate_queries(
        int orig_vfg_size, int *sccmap, AbstractQuery *indexing_method,
        vector<std::pair<int, int>> &reachable_pairs,
        vector<std::pair<int, int>> &unreachable_pairs) {
    if (read_query) {
        cout << "Reading queries from " << query_file << " ... " << endl;
        vector<std::pair<int, int>> *query_vec = &reachable_pairs;
        int s, t;
        ifstream iq(query_file);
        while (!iq.eof()) {
            iq >> s >> t;
            if (iq.eof())
                break;
            if (s == -1 && t == -1) { // -1, -1 is a separation line
                query_vec = &unreachable_pairs;
                continue;
            }
            query_vec->emplace_back(s, t);
        }
        iq.close();
    } else {
        cout << "Generating " << query_num * 2 << " queries..." << endl;
        int reachable_count = 0;
        int unreachable_count = 0;
        srand48(time(nullptr));
        ProgressBar bar(query_num * 2);
        while (reachable_count < query_num || unreachable_count < query_num) {
            int s = (int) lrand48() % (orig_vfg_size);
            int t = (int) lrand48() % (orig_vfg_size) + orig_vfg_size;

            if (indexing_method->reach(sccmap[s], sccmap[t])) {
                if (reachable_count < query_num) {
                    reachable_pairs.emplace_back(s, t - orig_vfg_size);
                    reachable_count++;
                    bar.update();
                }
            } else {
                if (unreachable_count < query_num) {
                    unreachable_pairs.emplace_back(s, t - orig_vfg_size);
                    unreachable_count++;
                    bar.update();
                }
            }
        }
        cout << "\rDone!" << endl;
        if (gen_query) {
            cout << "Saving queries into " << query_file << " ... " << endl;
            ofstream oq(query_file);
            for (const auto &re : reachable_pairs) {
                oq << re.first << " " << (re.second) << endl;
            }
            oq << "-1 -1" << endl; // use -1 -1 as a separation line
            for (const auto &ue : unreachable_pairs) {
                oq << ue.first << " " << (ue.second) << endl;
            }
            oq.close();
        }
    }

    cout << endl << endl;
}

static double grail_index_size(Graph &ig) {
    double ret = 0;
    for (int i = 0; i < ig.num_vertices(); i++) {
        ret += sizeof(int); // ig[i].top_level
        for (int j = 0; j < ig[i].pre->size(); ++j) {
            ret += sizeof(int) * 3; // ig[i].pre[j], ig[i].middle[j], ig[i].post[j]
        }
    }
    return ret / 1024.0 / 1024.0;
}

static double pt_index_size(Graph &bbgg, PathTree &pt, Query &pt_query) {
    double ret = 0;
    // backbone graph itself
    for (int i = 0; i < bbgg.num_vertices(); i++) {
        EdgeList &succs = bbgg.out_edges(i);
        for (int j = 0; j < succs.size(); ++j) {
            ret += sizeof(int); // succs[j]
        }
    }

    // pathtree labels
    for (int vid = 0; vid < pt.out_uncover.size(); vid++) {
        vector<int> &si = pt.out_uncover[vid];
        ret += sizeof(int); // vid
        for (int i = 0; i < si.size(); ++i) {
            ret += sizeof(int); // si[i]
        }
    }

    for (int i = 0; i < pt.g.num_vertices(); i++) {
        ret += sizeof(int); // i
        for (int j = 0; j < 3; j++) {
            ret += sizeof(int); // pt.labels[i][j]
        }
    }

    for (int i = 0; i < pt_query.graillabels.size(); ++i) {
        for (int j = 0; j < pt_query.grail_dim; ++j) {
            ret += sizeof(int) * 2; // pt_query.graillabels[i][j] -> pair<int, int>
        }
    }

    return ret / 1024.0 / 1024.0;
}

int main(int argc, char *argv[]) {
    parse_arg(argc, argv);

    ifstream graphfile(graph_file);
    Graph vfg(graphfile);
    graphfile.close();
    auto orig_vfg_size = vfg.num_vertices();
    auto orig_vfg_edges = vfg.num_edges();
    vfg.check(); // check the correctness

    auto start = std::chrono::high_resolution_clock::now();
    vfg.build_summary_edges();
    auto end = std::chrono::high_resolution_clock::now();
    chrono::duration<double, std::milli> diff = end - start;
    double summary_edge_time = diff.count();
    double summary_edge_size = ((double) vfg.summary_edge_size() * sizeof(int) * 2 / 1024 / 1024);
    vfg.to_indexing_graph();

    // merge strongly connected component
    int *sccmap = new int[vfg.num_vertices()]; // store pair of orignal vertex and corresponding vertex in merged graph
    vector<int> reverse_topo_sort;
    cout << "Merging strongly connected component of IG ..." << endl;
    start = std::chrono::high_resolution_clock::now();
    GraphUtil::mergeSCC(vfg, sccmap, reverse_topo_sort);
    end = std::chrono::high_resolution_clock::now();
    diff = end - start;
    cout << "Merging SCC of Indexing-Graph(IG) Duration: " << diff.count() << " ms" << endl;
    cout << "#DAG of IG: " << vfg.num_vertices() << " #DAG of IG Edges:" << vfg.num_edges() << endl;

    // GRAIL
    Grail *grail = nullptr;
    double grail_on_ig_duration = 0;
    double grail_on_ig_size = 0;
    if (indexing == "grail" || indexing == "pathtree+grail") {
        start = std::chrono::high_resolution_clock::now();
        GraphUtil::topo_leveler(vfg);
        grail = new Grail(vfg, grail_dim, 1, false, 100);
        end = std::chrono::high_resolution_clock::now();
        diff = end - start;
        grail_on_ig_duration = diff.count();
        grail_on_ig_size = grail_index_size(vfg);
        cout << "GRAIL Indexing Construction on IG Duration: " << grail_on_ig_duration << " ms" << endl;
    }

    // PATHTREE+SCARAB
    Query *pathtree = nullptr;
    double pt_total_duration = 0;
    double pt_total_size = 0;
    if (indexing == "pathtree" || indexing == "pathtree+grail") {
        // backbone
        int epsilon = bb_epsilon;
        double pr = 0.02;
        ReachBackbone rbb(vfg, epsilon - 1, pr, 1); // NR
        rbb.setBlockNum(5);
        start = std::chrono::high_resolution_clock::now();
        rbb.backboneDiscovery(2);
        end = std::chrono::high_resolution_clock::now();
        diff = end - start;
        double bb_discover_duration = diff.count();
        pt_total_duration = bb_discover_duration;
        cout << "Backbone Discover on IG Duration: " << bb_discover_duration << " ms" << endl;
        string filesystem = graph_file + ".backbone";
        rbb.outputBackbone(filesystem.c_str());
        int bbsize = rbb.getBBsize();
        int bbedgesize = rbb.getBBEdgesize();
        cout << "#Backbone of IG Vertices: " << bbsize << " #Backbone of IG Edges: " << bbedgesize << endl;

        // pathtree
        int pt_alg_type = 1;
        string ggfile = filesystem + "." + to_string(epsilon) + to_string((int) (pr * 1000)) + "gg";
        string labelsfile = filesystem + "." + "index";
        ifstream infile(ggfile);
        ifstream cfile;
        bool compress = false;
        if (!infile) {
            cout << "Error: Cannot open " << ggfile << endl;
            return -1;
        }
        Graph bbgg(infile); // backbone gated graph
        int bbggsize = bbgg.num_vertices();
        int *bbgg_sccmap = new int[bbggsize];    // store pair of orignal vertex and corresponding vertex in merged graph
        vector<int> bbgg_reverse_topo_sort;
        cout << "Merging SCC of Backbone ..." << endl;

        start = std::chrono::high_resolution_clock::now();
        GraphUtil::mergeSCC(bbgg, bbgg_sccmap, bbgg_reverse_topo_sort);
        end = std::chrono::high_resolution_clock::now();
        diff = end - start;
        pt_total_duration += diff.count();
        cout << "Merging SCC of Backbone Duration: " << diff.count() << " ms" << endl;
        cout << "#DAG of BB Vertices: " << bbgg.num_vertices() << " #DAG of BB Edges: " << bbgg.num_edges() << endl;

        cout << "Constructing Pathtree (PT) Indexing ..." << endl;
        start = std::chrono::high_resolution_clock::now();
        PathTree pt(bbgg, bbgg_reverse_topo_sort);
        pt.createLabels(pt_alg_type, cfile, compress);
        end = std::chrono::high_resolution_clock::now();
        diff = end - start;
        auto pt_on_bb_duration = diff.count();
        pt_total_duration += diff.count();
        cout << "#PT Indexing Construction Duration: " << pt_on_bb_duration << " ms" << endl;
        ofstream lfile(labelsfile);
        pt.save_labels(lfile);

        // query utility of path tree
        double grail_on_bb_duration;
        pathtree = new PathtreeQuery(filesystem.c_str(), vfg, epsilon, pr, true, &grail_on_bb_duration);
        pt_total_duration += grail_on_bb_duration;
        pt_total_size = pt_index_size(bbgg, pt, *pathtree);
    }

    // prepare queries
    vector<std::pair<int, int>> reachable_pairs;
    vector<std::pair<int, int>> unreachable_pairs;
    if (pathtree) {
        read_or_generate_queries(orig_vfg_size, sccmap, pathtree, reachable_pairs, unreachable_pairs);
    } else if (grail) {
        read_or_generate_queries(orig_vfg_size, sccmap, grail, reachable_pairs, unreachable_pairs);
    } else {
        assert(false);
    }

    double grail_r_time = 0;
    double grail_nr_time = 0;
    if (grail) {
        cout << "\n--------- GRAIL Queries Test ------------" << endl;
        auto src_map = [sccmap](int v) { return sccmap[v]; };
        auto trg_map = [sccmap, orig_vfg_size](int v) { return sccmap[v + orig_vfg_size]; };
        grail_r_time = test_query(grail, reachable_pairs, true, src_map, trg_map);
        grail_nr_time = test_query(grail, unreachable_pairs, false, src_map, trg_map);
    }

    double pt_r_time = 0;
    double pt_nr_time = 0;
    if (pathtree) {
        cout << "--------- Pathtree Queries Test ------------" << endl;
        auto src_map = [sccmap](int v) { return sccmap[v]; };
        auto trg_map = [sccmap, orig_vfg_size](int v) { return sccmap[v + orig_vfg_size]; };
        pt_r_time = test_query(pathtree, reachable_pairs, true, src_map, trg_map);
        pt_nr_time = test_query(pathtree, unreachable_pairs, false, src_map, trg_map);
    }

    double tab_r_query_time = 0;
    double tab_notr_query_time = 0;
    double tc_time = 0;
    double tc_size = 0;
    if (reps_tab_alg || transitive_closure) {
        ifstream orig_gf(graph_file);
        Graph orig_vfg(orig_gf);
        orig_gf.close();
        orig_vfg.build_summary_edges();
        orig_vfg.add_summary_edges();

        if (reps_tab_alg) {
            cout << "--------- Tabulation Queries Test ------------" << endl;
            auto *tab = new Tabulation(orig_vfg);
            auto vertex_map = [](int v) { return v; };
            tab_r_query_time = test_query(tab, reachable_pairs, true, vertex_map, vertex_map);
            tab_notr_query_time = test_query(tab, unreachable_pairs, false, vertex_map, vertex_map);
            delete tab;
        }

        if (transitive_closure) {
            cout << "--------- Transitive Closure ---------" << endl;
            start = std::chrono::high_resolution_clock::now();
            tc_size = Tabulation(orig_vfg).tc();
            end = std::chrono::high_resolution_clock::now();
            diff = end - start;
            tc_time = diff.count();
            cout << "\rTransitive closure time: " << std::setprecision(2) << fixed << tc_time << " ms. " << endl;
            cout << "Transitive closure size: " << std::setprecision(2) << fixed << tc_size << " mb. " << endl;
        }
    }

    cout << "--------- Indexing Construction Summary ---------" << endl;
    cout << "# Vertices: " << orig_vfg_size << endl;
    cout << "# Edges: " << orig_vfg_edges << endl;
    cout << "# Summary Edges: " << vfg.summary_edge_size() << endl;
    cout << "Summary Edge     time: " << std::setprecision(2) << fixed << summary_edge_time << " ms. " << endl;
    cout << "Summary Edge     size: " << std::setprecision(2) << fixed << summary_edge_size << " mb." << endl;
    if (grail) {
        cout << "GRAIL    indices time: " << std::setprecision(2) << fixed << grail_on_ig_duration << " ms. " << endl;
        cout << "GRAIL    indices size: " << std::setprecision(2) << fixed << grail_on_ig_size << " mb. " << endl;
        delete grail;
    }
    if (pathtree) {
        cout << "Pathtree indices time: " << std::setprecision(2) << fixed << pt_total_duration << " ms. " << endl;
        cout << "Pathtree indices size: " << std::setprecision(2) << fixed << pt_total_size << " mb. " << endl;
        delete pathtree;
    }

    // final output
    cout << endl;
    cerr << std::setprecision(2) << fixed << orig_vfg_edges / 1000.0 << ", ";
    cerr << std::setprecision(2) << fixed << orig_vfg_size / 1000.0 << ", ";
    cerr << std::setprecision(2) << fixed << summary_edge_time / 1000.0 << ", ";
    cerr << std::setprecision(2) << fixed << pt_total_duration / 1000.0 << ", ";
    cerr << std::setprecision(2) << fixed << grail_on_ig_duration / 1000.0 << ", ";
    cerr << ", ";
    cerr << std::setprecision(2) << fixed << summary_edge_size << ", ";
    cerr << std::setprecision(2) << fixed << pt_total_size << ", ";
    cerr << std::setprecision(2) << fixed << grail_on_ig_size << ", ";
    cerr << ", ";
    cerr << std::setprecision(2) << fixed << tc_time / 1000.0 << ", ";
    cerr << std::setprecision(2) << fixed << tc_size << ", ";
    cerr << std::setprecision(2) << fixed << pt_r_time << ", ";
    cerr << std::setprecision(2) << fixed << pt_nr_time << ", ";
    cerr << ", ";
    cerr << std::setprecision(2) << fixed << grail_r_time << ", ";
    cerr << std::setprecision(2) << fixed << grail_nr_time << ", ";
    cerr << ", ";
    cerr << std::setprecision(2) << fixed << tab_r_query_time << ", ";
    cerr << std::setprecision(2) << fixed << tab_notr_query_time << ", ";
    cerr << ", ";
    cerr << endl;

    return 0;
}

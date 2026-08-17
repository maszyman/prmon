// Copyright (C) 2026 CERN
// License Apache2 - see LICENCE file

#include "numamon.h"

#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <sstream>

#include "utils.h"

#define MONITOR_NAME "numamon"

namespace {
const std::string page_size_key{"kernelpagesize_kB="};
}  // namespace

// Constructor; uses RAII pattern to be valid
// after construction
numamon::numamon(unsigned int mem_sample_interval_s)
    : numa_stats{}, mem_sample_interval{mem_sample_interval_s} {
  log_init(MONITOR_NAME);
#undef MONITOR_NAME
  for (const auto& param : params) {
    numa_stats.emplace(param.get_name(), prmon::monitored_value(param));
  }
  // Read the machine's topology once - it cannot change while we run
  if (load_topology("") < 2) {
    debug("Fewer than two NUMA nodes found, NUMA monitoring is disabled");
  }
}

void numamon::update_stats(const std::vector<pid_t>& pids,
                           const std::string read_path) {
  // Compute locality: the nodes the job's threads are spread over right now
  const auto threads_per_node = get_threads_per_node(read_path, pids);
  numa_stats.at("numa_cpu_spread").set_value(threads_per_node.size());

  // numa_mem_local_pct is measured on a slower cadence than numa_cpu_spread
  const auto now = std::chrono::steady_clock::now();
  if (mem_sampled && now - last_mem_sample < mem_sample_interval) return;

  const auto node_kb = get_kb_per_node(read_path, pids);

  unsigned long long total_kb{0};
  for (const auto& node : node_kb) {
    total_kb += node.second;
  }

  // Weight each node's memory by the threads on it - membership alone would
  // score a job spread over every node as 100% local
  unsigned long long n_threads{0};
  unsigned long long thread_local_kb{0};
  for (const auto& node : threads_per_node) {
    n_threads += node.second;
    const auto kb = node_kb.find(node.first);
    if (kb != node_kb.end()) {
      thread_local_kb += node.second * kb->second;
    }
  }

  // With nothing to measure, leave the value alone so the average covers
  // only the cycles that did measure
  if (total_kb == 0 || n_threads == 0) return;

  // The share local to the average thread, i.e. the expected fraction of
  // its accesses that stay on-node
  numa_stats.at("numa_mem_local_pct")
      .set_value(100 * thread_local_kb / (n_threads * total_kb));
  last_mem_sample = now;
  mem_sampled = true;
}

// Read the CPU -> NUMA node mapping from sysfs
size_t numamon::load_topology(const std::string& read_path) {
  cpu_to_node.clear();
  node_cpulist.clear();

  const std::string node_path{read_path + "/sys/devices/system/node"};
  DIR* node_dir = opendir(node_path.c_str());
  if (node_dir == nullptr) return 0;

  struct dirent* entry{};
  while ((entry = readdir(node_dir)) != nullptr) {
    // Node ids come from the node<N> directories; the other entries in here
    // (online, possible, power, ...) are not nodes
    std::string name{entry->d_name};
    if (name.compare(0, 4, "node") != 0) continue;
    int node{};
    if (!prmon::to_number(name.substr(4), node)) continue;

    std::ifstream cpulist_file{node_path + "/" + name + "/cpulist"};
    std::string cpulist{};
    // A memory-only node has an empty CPU list but still counts
    std::getline(cpulist_file, cpulist);
    node_cpulist[node] = cpulist;
    expand_cpu_range(node, cpulist);
  }
  closedir(node_dir);

  return node_cpulist.size();
}

void numamon::expand_cpu_range(int node, const std::string& range) {
  std::stringstream range_stream{range};
  std::string token{};
  while (std::getline(range_stream, token, ',')) {
    if (token.empty()) continue;
    size_t dash_idx = token.find('-');
    int first{};
    int last{};
    if (dash_idx == std::string::npos) {
      if (prmon::to_number(token, first)) cpu_to_node[first] = node;
    } else if (prmon::to_number(token.substr(0, dash_idx), first) &&
               prmon::to_number(token.substr(dash_idx + 1), last)) {
      for (int cpu = first; cpu <= last; ++cpu) cpu_to_node[cpu] = node;
    }
  }
}

std::map<int, unsigned long long> numamon::get_threads_per_node(
    const std::string& read_path, const std::vector<pid_t>& pids) {
  std::map<int, unsigned long long> nodes{};
  for (const auto pid : pids) {
    const std::string task_path{read_path + "/proc/" + std::to_string(pid) +
                                "/task"};
    DIR* task_dir = opendir(task_path.c_str());
    // The process may have exited between being listed and being read
    if (task_dir == nullptr) continue;
    struct dirent* entry{};
    while ((entry = readdir(task_dir)) != nullptr) {
      // Anything that is not a number is not a thread ("." and "..")
      std::string tid{entry->d_name};
      pid_t tid_value{};
      if (!prmon::to_number(tid, tid_value)) continue;
      auto node_it =
          cpu_to_node.find(get_thread_processor(read_path, pid, tid));
      if (node_it != cpu_to_node.end()) ++nodes[node_it->second];
    }
    closedir(task_dir);
  }
  return nodes;
}

// The CPU is field 39 ("processor") of proc_pid_stat(5). Field 2, the thread
// name, is parenthesised but can contain spaces (e.g. "Web Content"), which
// shifts every later field, so count from after the last ')', not the start.
int numamon::get_thread_processor(const std::string& read_path, pid_t pid,
                                  const std::string& tid) {
  std::ifstream task_stat{read_path + "/proc/" + std::to_string(pid) +
                          "/task/" + tid + "/stat"};
  std::string line{};
  if (!std::getline(task_stat, line)) return -1;

  size_t comm_end = line.rfind(')');
  if (comm_end == std::string::npos) return -1;
  std::stringstream fields{line.substr(comm_end + 1)};

  std::string field{};
  for (size_t pos = 0; pos <= prmon::processor_pos_after_comm; ++pos) {
    if (!(fields >> field)) return -1;
  }
  int cpu{};
  return prmon::to_number(field, cpu) ? cpu : -1;
}

// numa_maps has one line per mapping, with an N<node>=<pages> token per node
// it has pages on. The counts are in the mapping's own page size, so they are
// weighted by kernelpagesize_kB to keep huge page mappings comparable.
std::map<int, unsigned long long> numamon::get_kb_per_node(
    const std::string& read_path, const std::vector<pid_t>& pids) {
  std::map<int, unsigned long long> node_kb{};
  std::string line{};
  std::string token{};
  std::vector<std::pair<int, unsigned long long>> line_pages{};
  for (const auto pid : pids) {
    std::ifstream numa_maps{read_path + "/proc/" + std::to_string(pid) +
                            "/numa_maps"};
    while (std::getline(numa_maps, line)) {
      line_pages.clear();
      // If the mapping does not say, assume the usual 4 kB pages
      unsigned long long page_size_kb{4};
      std::stringstream line_stream{line};
      while (line_stream >> token) {
        // Only the per-node counts start with an upper case N, which rejects
        // most tokens of these large files cheaply
        if (token[0] == 'N') {
          size_t equals_idx = token.find('=');
          if (equals_idx == std::string::npos) continue;
          int node{};
          unsigned long long pages{};
          if (prmon::to_number(token.substr(1, equals_idx - 1), node) &&
              prmon::to_number(token.substr(equals_idx + 1), pages)) {
            line_pages.emplace_back(node, pages);
          }
        } else if (token.compare(0, page_size_key.size(), page_size_key) == 0) {
          // This token comes after the N<node> ones, hence the two step
          prmon::to_number(token.substr(page_size_key.size()), page_size_kb);
        }
      }
      for (const auto& node : line_pages) {
        node_kb[node.first] += node.second * page_size_kb;
      }
    }
  }
  return node_kb;
}

// Return the snapshot values
prmon::monitored_value_map const numamon::get_text_stats() {
  prmon::monitored_value_map numa_stat_map{};
  for (const auto& value : numa_stats) {
    numa_stat_map[value.first] = value.second.get_value();
  }
  return numa_stat_map;
}

// For JSON return the peaks
prmon::monitored_value_map const numamon::get_json_total_stats() {
  prmon::monitored_value_map numa_max_stat_map{};
  for (const auto& value : numa_stats) {
    numa_max_stat_map[value.first] = value.second.get_max_value();
  }
  return numa_max_stat_map;
}

// And the averages
prmon::monitored_average_map const numamon::get_json_average_stats(
    unsigned long long elapsed_clock_ticks) {
  prmon::monitored_average_map numa_avg_stat_map{};
  for (const auto& value : numa_stats) {
    numa_avg_stat_map[value.first] = value.second.get_average_value();
  }
  return numa_avg_stat_map;
}

// Return the parameter list
prmon::parameter_list const numamon::get_parameter_list() { return params; }

// Collect related hardware information
void const numamon::get_hardware_info(nlohmann::json& hw_json) {
  // numa_cpu_spread has no scale without the node count that bounds it
  hw_json["HW"]["numa"]["Nodes"] = node_cpulist.size();
  for (const auto& node : node_cpulist) {
    hw_json["HW"]["numa"]["node" + std::to_string(node.first)]["CPUs"] =
        node.second;
  }
  return;
}

void const numamon::get_unit_info(nlohmann::json& unit_json) {
  prmon::fill_units(unit_json, params);
  return;
}

// Copyright (C) 2018-2025 CERN
// License Apache2 - see LICENCE file

// CPU monitoring class
//

#ifndef PRMON_CPUMON_H
#define PRMON_CPUMON_H 1

#include <map>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

#include "Imonitor.h"
#include "MessageBase.h"
#include "parameter.h"
#include "registry.h"

class cpumon final : public Imonitor, public MessageBase {
 private:
  // Setup the parameters to monitor here
  const prmon::parameter_list params = {
      {"utime", "s", ""},
      {"stime", "s", ""},
      {"numa_nodes_used", "1", ""},
      {"numa_migrations", "1", ""},
      {"numa_mem_nodes_used", "1", ""},
      {"numa_mem_migrations", "1", ""}};

  // Dynamic monitoring container for value measurements
  // This will be filled at initialisation, taking the names
  // from the above params
  prmon::monitored_list cpu_stats;

  // CPU -> NUMA node map, filled from 'lscpu' output in parse_lscpu_output()
  std::unordered_map<int, int> cpu_to_node;

  // Compute-locality tracking state, accumulated across the job's life
  std::set<int> nodes_seen;
  std::unordered_map<pid_t, int> last_seen_node;
  unsigned long long numa_migrations{0};

  // Memory-locality tracking state, accumulated across the job's life
  std::set<int> mem_nodes_seen;
  int last_dominant_mem_node{-1};
  unsigned long long numa_mem_migrations{0};

  // Expand a CPU range string, e.g. "0-3,8-11", into individual CPUs and
  // record their NUMA node in cpu_to_node
  void expand_cpu_range(int node, const std::string& range);

  // Return the thread IDs found under <read_path>/proc/<pid>/task
  std::vector<pid_t> get_thread_ids(const std::string& read_path, pid_t pid);

  // Return the CPU a thread last ran on (field 39 of its stat file),
  // or -1 if unavailable
  int get_thread_processor(const std::string& read_path, pid_t pid, pid_t tid);

  // Read <read_path>/proc/<pid>/numa_maps and accumulate per-node resident
  // page counts into node_pages
  void read_numa_maps_node_pages(
      const std::string& read_path, pid_t pid,
      std::unordered_map<int, unsigned long long>& node_pages);

 public:
  cpumon();

  void update_stats(const std::vector<pid_t>& pids,
                    const std::string read_path = "");

  // These are the stat getter methods which retrieve current statistics
  prmon::monitored_value_map const get_text_stats();
  prmon::monitored_value_map const get_json_total_stats();
  prmon::monitored_average_map const get_json_average_stats(
      unsigned long long elapsed_clock_ticks);
  prmon::parameter_list const get_parameter_list();

  // This is the hardware information getter that runs once
  void const get_hardware_info(nlohmann::json& hw_json);

  // Parses the output lines of 'lscpu' into the hardware info JSON;
  // exposed separately so it can be exercised directly in unit tests
  // without needing 'lscpu' itself to be present
  void const parse_lscpu_output(const std::vector<std::string>& lscpu_lines,
                                nlohmann::json& hw_json);

  void const get_unit_info(nlohmann::json& unit_json);
  bool const is_valid() { return true; }
};
REGISTER_MONITOR(Imonitor, cpumon, "Monitor cpu time used")

#endif  // PRMON_CPUMON_H

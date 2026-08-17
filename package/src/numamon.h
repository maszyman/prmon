// Copyright (C) 2026 CERN
// License Apache2 - see LICENCE file

// NUMA locality monitoring class
//

#ifndef PRMON_NUMAMON_H
#define PRMON_NUMAMON_H 1

#include <chrono>
#include <map>
#include <string>
#include <unordered_map>
#include <vector>

#include "Imonitor.h"
#include "MessageBase.h"
#include "parameter.h"
#include "registry.h"

class numamon final : public Imonitor, public MessageBase {
 private:
  // Setup the parameters to monitor here
  // Both metrics are snapshots
  const prmon::parameter_list params = {{"numa_cpu_spread", "1", "1"},
                                        {"numa_mem_local_pct", "%", "%"}};

  // Dynamic monitoring container for value measurements
  // This will be filled at initialisation, taking the names
  // from the above params
  prmon::monitored_list numa_stats;

  // CPU -> NUMA node map and each node's raw CPU list, read once from sysfs
  std::unordered_map<int, int> cpu_to_node;
  std::map<int, std::string> node_cpulist;

  // Reading numa_maps walks the page tables of every mapping, so it gets its
  // own slower cadence; the cycles in between record nothing at all
  const std::chrono::seconds mem_sample_interval;
  std::chrono::steady_clock::time_point last_mem_sample{};
  bool mem_sampled{false};

  // Threads of the processes in <pids>, by the NUMA node they last ran on
  std::map<int, unsigned long long> get_threads_per_node(
      const std::string& read_path, const std::vector<pid_t>& pids);

  // Return the CPU thread <tid> last ran on, or -1 if that is unavailable
  int get_thread_processor(const std::string& read_path, pid_t pid,
                           const std::string& tid);

  // Resident memory in kB of the processes in <pids>, by NUMA node
  std::map<int, unsigned long long> get_kb_per_node(
      const std::string& read_path, const std::vector<pid_t>& pids);

  // Record <node> as the node of every CPU in <range>, e.g. "0-3,8-11"
  void expand_cpu_range(int node, const std::string& range);

 public:
  explicit numamon(unsigned int mem_sample_interval_s = 30);

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
  void const get_unit_info(nlohmann::json& unit_json);

  // Read the CPU -> NUMA node map from <read_path>/sys/devices/system/node
  // and return the node count; public so tests can use precooked sources
  size_t load_topology(const std::string& read_path);

  // Fewer than two nodes means no locality to measure, so the parameters are
  // omitted from the output
  bool const is_valid() { return node_cpulist.size() > 1; }
};
REGISTER_MONITOR(Imonitor, numamon, "Monitor NUMA locality of CPU and memory")

#endif  // PRMON_NUMAMON_H

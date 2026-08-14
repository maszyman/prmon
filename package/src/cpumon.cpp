// Copyright (C) 2018-2025 CERN
// License Apache2 - see LICENCE file

#include "cpumon.h"

#include <dirent.h>
#include <string.h>
#include <unistd.h>

#include <fstream>
#include <iostream>
#include <regex>
#include <sstream>

#include "utils.h"

#define MONITOR_NAME "cpumon"

// Constructor; uses RAII pattern to be valid
// after construction
cpumon::cpumon() : cpu_stats{} {
  log_init(MONITOR_NAME);
#undef MONITOR_NAME
  for (const auto& param : params) {
    cpu_stats.emplace(param.get_name(), prmon::monitored_value(param, true));
  }
}

void cpumon::update_stats(const std::vector<pid_t>& pids,
                          const std::string read_path) {
  unsigned long long utime_total{0}, stime_total{0};
  std::vector<std::string> stat_entries{};
  stat_entries.reserve(prmon::stat_cpu_read_limit + 1);
  std::string tmp_str{};
  for (const auto pid : pids) {
    std::stringstream stat_fname{};
    stat_fname << read_path << "/proc/" << pid << "/stat" << std::ends;
    std::ifstream proc_stat{stat_fname.str()};
    while (proc_stat && stat_entries.size() < prmon::stat_cpu_read_limit + 1) {
      proc_stat >> tmp_str;
      if (proc_stat) stat_entries.push_back(tmp_str);
    }
    if (stat_entries.size() > prmon::stat_cpu_read_limit) {
      utime_total += std::stol(stat_entries[prmon::utime_pos]) +
                     std::stol(stat_entries[prmon::cutime_pos]);
      stime_total += std::stol(stat_entries[prmon::stime_pos]) +
                     std::stol(stat_entries[prmon::cstime_pos]);
    }
    stat_entries.clear();
  }
  cpu_stats.at("utime").set_value(utime_total / sysconf(_SC_CLK_TCK));
  cpu_stats.at("stime").set_value(stime_total / sysconf(_SC_CLK_TCK));

  // Compute locality: for every thread, check the CPU it last ran on and
  // map it to a NUMA node via the table built from 'lscpu' at startup. Skip
  // entirely if that table is empty (hw-info collection suppressed or
  // 'lscpu' failed) - there's nothing to map CPUs to in that case.
  if (!cpu_to_node.empty()) {
    for (const auto pid : pids) {
      for (const auto tid : get_thread_ids(read_path, pid)) {
        int cpu = get_thread_processor(read_path, pid, tid);
        auto node_it = cpu_to_node.find(cpu);
        if (cpu < 0 || node_it == cpu_to_node.end()) continue;
        int node = node_it->second;
        nodes_seen.insert(node);
        auto last_it = last_seen_node.find(tid);
        if (last_it != last_seen_node.end() && last_it->second != node) {
          ++numa_migrations;
        }
        last_seen_node[tid] = node;
      }
    }
  }
  cpu_stats.at("numa_nodes_used").set_value(nodes_seen.size());
  cpu_stats.at("numa_migrations").set_value(numa_migrations);

  // Memory locality: accumulate per-node resident page counts across all
  // pids in the job tree for this cycle, from 'numa_maps'.
  std::unordered_map<int, unsigned long long> node_pages{};
  for (const auto pid : pids) {
    read_numa_maps_node_pages(read_path, pid, node_pages);
  }
  if (!node_pages.empty()) {
    int dominant_node = -1;
    unsigned long long dominant_pages = 0;
    for (const auto& node_count : node_pages) {
      mem_nodes_seen.insert(node_count.first);
      if (node_count.second > dominant_pages) {
        dominant_pages = node_count.second;
        dominant_node = node_count.first;
      }
    }
    if (last_dominant_mem_node != -1 && dominant_node != last_dominant_mem_node) {
      ++numa_mem_migrations;
    }
    last_dominant_mem_node = dominant_node;
  }
  cpu_stats.at("numa_mem_nodes_used").set_value(mem_nodes_seen.size());
  cpu_stats.at("numa_mem_migrations").set_value(numa_mem_migrations);
}

// Parse a CPU range string, e.g. "0-3,8-11", into individual CPUs and
// record their NUMA node in cpu_to_node
void cpumon::expand_cpu_range(int node, const std::string& range) {
  std::stringstream range_stream{range};
  std::string token{};
  while (std::getline(range_stream, token, ',')) {
    if (token.empty()) continue;
    size_t dashIdx = token.find('-');
    if (dashIdx == std::string::npos) {
      cpu_to_node[std::stoi(token)] = node;
    } else {
      int start = std::stoi(token.substr(0, dashIdx));
      int end = std::stoi(token.substr(dashIdx + 1));
      for (int cpu = start; cpu <= end; ++cpu) cpu_to_node[cpu] = node;
    }
  }
}

// Return the thread IDs found under <read_path>/proc/<pid>/task
std::vector<pid_t> cpumon::get_thread_ids(const std::string& read_path,
                                          pid_t pid) {
  std::vector<pid_t> tids{};
  std::stringstream task_dir_name{};
  task_dir_name << read_path << "/proc/" << pid << "/task" << std::ends;
  DIR* task_dir = opendir(task_dir_name.str().c_str());
  if (task_dir == nullptr) return tids;
  struct dirent* entry{};
  while ((entry = readdir(task_dir)) != nullptr) {
    std::string name{entry->d_name};
    if (name == "." || name == "..") continue;
    try {
      tids.push_back(static_cast<pid_t>(std::stoi(name)));
    } catch (const std::exception&) {
      continue;
    }
  }
  closedir(task_dir);
  return tids;
}

// Return the CPU a thread last ran on (field 39, "processor", of its
// /proc/[pid]/task/[tid]/stat), or -1 if unavailable
int cpumon::get_thread_processor(const std::string& read_path, pid_t pid,
                                 pid_t tid) {
  std::stringstream stat_fname{};
  stat_fname << read_path << "/proc/" << pid << "/task/" << tid << "/stat"
             << std::ends;
  std::ifstream task_stat{stat_fname.str()};
  std::vector<std::string> stat_entries{};
  stat_entries.reserve(prmon::stat_task_read_limit + 1);
  std::string tmp_str{};
  while (task_stat && stat_entries.size() < prmon::stat_task_read_limit + 1) {
    task_stat >> tmp_str;
    if (task_stat) stat_entries.push_back(tmp_str);
  }
  if (stat_entries.size() <= prmon::stat_task_read_limit) return -1;
  try {
    return std::stoi(stat_entries[prmon::processor_pos]);
  } catch (const std::exception&) {
    return -1;
  }
}

// Read <read_path>/proc/<pid>/numa_maps and accumulate per-node resident
// page counts into node_pages (summed across all VMAs in the file)
void cpumon::read_numa_maps_node_pages(
    const std::string& read_path, pid_t pid,
    std::unordered_map<int, unsigned long long>& node_pages) {
  std::stringstream numa_maps_fname{};
  numa_maps_fname << read_path << "/proc/" << pid << "/numa_maps"
                  << std::ends;
  std::ifstream numa_maps{numa_maps_fname.str()};
  const std::regex node_token_re("N(\\d+)=(\\d+)");
  std::string line{};
  while (std::getline(numa_maps, line)) {
    std::stringstream line_stream{line};
    std::string token{};
    while (line_stream >> token) {
      std::smatch node_match;
      if (std::regex_match(token, node_match, node_token_re)) {
        node_pages[std::stoi(node_match[1].str())] +=
            std::stoull(node_match[2].str());
      }
    }
  }
}

// Return the summed counters
prmon::monitored_value_map const cpumon::get_text_stats() {
  prmon::monitored_value_map cpu_stat_map{};
  for (const auto& value : cpu_stats) {
    cpu_stat_map[value.first] = value.second.get_value();
  }
  return cpu_stat_map;
}

// Same for JSON
prmon::monitored_value_map const cpumon::get_json_total_stats() {
  return cpumon::get_text_stats();
}

// For CPU time there's nothing to return for an average
prmon::monitored_average_map const cpumon::get_json_average_stats(
    unsigned long long elapsed_clock_ticks) {
  static const prmon::monitored_average_map empty_average_stats{};
  return empty_average_stats;
}

// Return the parameter list
prmon::parameter_list const cpumon::get_parameter_list() { return params; }

// Collect related hardware information
void const cpumon::get_hardware_info(nlohmann::json& hw_json) {
  // Define the command and run it
  const std::vector<std::string> cmd = {"lscpu"};

  auto cmd_result = prmon::cmd_pipe_output(cmd);

  // If the command failed print an error and move on
  if (cmd_result.first) {
    error("Failed to execute 'lscpu' to get CPU information (code " +
          std::to_string(cmd_result.first) + ")");
    return;
  }

  parse_lscpu_output(cmd_result.second, hw_json);

  return;
}

// Parse the lines of 'lscpu' output into the hardware info JSON
void const cpumon::parse_lscpu_output(
    const std::vector<std::string>& lscpu_lines, nlohmann::json& hw_json) {
  // Map lscpu names to the desired ones in the JSON
  const std::unordered_map<std::string, std::string> metricToName{
      {"Model name", "ModelName"},
      {"CPU(s)", "CPUs"},
      {"Socket(s)", "Sockets"},
      {"Core(s) per socket", "CoresPerSocket"},
      {"Thread(s) per core", "ThreadsPerCore"}};

  // Useful function to determine if a string is purely a number
  auto isNumber = [](const std::string& s) {
    return !s.empty() && std::find_if(s.begin(), s.end(), [](unsigned char c) {
                           return !std::isdigit(c);
                         }) == s.end();
  };

  // Loop over the output, parse the line, check the key and store the value if
  // requested
  std::string key{}, value{};

  for (const auto& line : lscpu_lines) {
    // Continue on empty line
    if (line.empty()) continue;

    // Tokenize by ":"
    size_t splitIdx = line.find(":");
    if (splitIdx == std::string::npos) continue;

    // Read "key":"value" pairs
    key = line.substr(0, splitIdx);
    value = line.substr(splitIdx + 1);
    if (key.empty() || value.empty()) continue;
    key = std::regex_replace(key, std::regex("^\\s+|\\s+$"), "");
    value = std::regex_replace(value, std::regex("^\\s+|\\s+$"), "");

    // Fill the JSON with the information
    if (metricToName.count(key) == 1) {
      if (isNumber(value))
        hw_json["HW"]["cpu"][metricToName.at(key)] = std::stoi(value);
      else
        hw_json["HW"]["cpu"][metricToName.at(key)] = value;
    } else if (key == "NUMA node(s)" && isNumber(value)) {
      hw_json["HW"]["cpu"]["NumaNodes"] = std::stoi(value);
    } else {
      std::smatch numaNodeMatch;
      if (std::regex_match(key, numaNodeMatch,
                            std::regex("NUMA node(\\d+) CPU\\(s\\)"))) {
        hw_json["HW"]["cpu"]["node" + numaNodeMatch[1].str()]["CPUs"] = value;
        expand_cpu_range(std::stoi(numaNodeMatch[1].str()), value);
      }
    }
  }

  return;
}

void const cpumon::get_unit_info(nlohmann::json& unit_json) {
  prmon::fill_units(unit_json, params);
  return;
}

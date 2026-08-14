#include <iostream>
#include <sstream>
#include <string>

#include "../../prmonVersion.h"
#include "../src/Imonitor.h"
#include "../src/countmon.h"
#include "../src/cpumon.h"
#include "../src/iomon.h"
#include "../src/memmon.h"
#include "../src/netmon.h"
#include "../src/nvidiamon.h"
#include "../src/prmonutils.h"
#include "../src/registry.h"
#include "gtest/gtest.h"

const std::vector<pid_t> mother_pid{1729};

#define TO_STRING2(X) #X
#define TO_STRING(X) TO_STRING2(X)

const std::string base_path = TO_STRING(TESTS_SOURCE_DIR);

std::shared_ptr<spdlog::sinks::stdout_color_sink_st> c_sink{
    std::make_shared<spdlog::sinks::stdout_color_sink_st>()};
std::shared_ptr<spdlog::sinks::basic_file_sink_st> f_sink{
    std::make_shared<spdlog::sinks::basic_file_sink_st>("prmon.log", true)};

bool prmon::sigusr1 = false;

TEST(IomonTest, IomonMonitonicityTestFixed) {
  std::string cur_path = base_path + "drop";
  std::vector<pid_t> fake_pids = mother_pid;

  std::unique_ptr<Imonitor> monitor(
      registry::Registry<Imonitor>::create("iomon"));

  const int iterationCount = 3;

  std::map<std::string, unsigned long long> store_stats;
  for (int iteration = 1; iteration <= iterationCount; ++iteration) {
    std::stringstream iteration_path{};
    iteration_path << cur_path << "/" << iteration;
    monitor->update_stats(fake_pids, iteration_path.str());
    store_stats = monitor->get_text_stats();
    for (auto stat : store_stats) {
      if (iteration == 1) {
        ASSERT_EQ(stat.second, 500);
      } else if (iteration == 2) {
        ASSERT_EQ(stat.second, 1000);
      } else {
        ASSERT_EQ(stat.second, 1000);
      }
    }
  }
}

TEST(CpumonTest, CpumonMonitonicityTestFixed) {
  std::string cur_path = base_path + "drop";
  std::vector<pid_t> fake_pids = mother_pid;

  std::unique_ptr<Imonitor> monitor(
      registry::Registry<Imonitor>::create("cpumon"));

  const int iterationCount = 3;

  std::map<std::string, unsigned long long> store_stats;
  for (int iteration = 1; iteration <= iterationCount; ++iteration) {
    std::stringstream iteration_path{};
    iteration_path << cur_path << "/" << iteration;
    monitor->update_stats(fake_pids, iteration_path.str());
    store_stats = monitor->get_text_stats();
    unsigned long long expected_cpu_time =
        (iteration == 1) ? (5000000 * 2) / sysconf(_SC_CLK_TCK)
                          : (10000000 * 2) / sysconf(_SC_CLK_TCK);
    ASSERT_EQ(store_stats["utime"], expected_cpu_time);
    ASSERT_EQ(store_stats["stime"], expected_cpu_time);
  }
}

TEST(CpumonTest, ParseLscpuOutputFixed) {
  cpumon monitor{};
  nlohmann::json hw_json;

  const std::vector<std::string> fake_lscpu_output{
      "Architecture:            x86_64",
      "CPU(s):                  8",
      "Thread(s) per core:      2",
      "Core(s) per socket:      4",
      "Socket(s):               1",
      "Model name:              Fake CPU Model",
      "NUMA:",
      "  NUMA node(s):          2",
      "  NUMA node0 CPU(s):     0-3",
      "  NUMA node2 CPU(s):     4-7"};

  monitor.parse_lscpu_output(fake_lscpu_output, hw_json);

  ASSERT_EQ(hw_json["HW"]["cpu"]["ModelName"], "Fake CPU Model");
  ASSERT_EQ(hw_json["HW"]["cpu"]["CPUs"], 8);
  ASSERT_EQ(hw_json["HW"]["cpu"]["Sockets"], 1);
  ASSERT_EQ(hw_json["HW"]["cpu"]["CoresPerSocket"], 4);
  ASSERT_EQ(hw_json["HW"]["cpu"]["ThreadsPerCore"], 2);

  ASSERT_EQ(hw_json["HW"]["cpu"]["NumaNodes"], 2);
  ASSERT_EQ(hw_json["HW"]["cpu"]["node0"]["CPUs"], "0-3");
  ASSERT_EQ(hw_json["HW"]["cpu"]["node2"]["CPUs"], "4-7");
  ASSERT_EQ(hw_json["HW"]["cpu"].count("node1"), 0u);
}

TEST(CpumonTest, NumaLocalityTestFixed) {
  std::string cur_path = base_path + "drop";
  std::vector<pid_t> fake_pids = mother_pid;

  // Seed the CPU -> NUMA node table the same way get_hardware_info() would,
  // without needing 'lscpu' itself to be present
  cpumon monitor{};
  nlohmann::json hw_json;
  const std::vector<std::string> fake_lscpu_output{
      "NUMA node(s):          2", "NUMA node0 CPU(s):     0-3",
      "NUMA node2 CPU(s):     4-7"};
  monitor.parse_lscpu_output(fake_lscpu_output, hw_json);

  const int iterationCount = 3;
  // Thread 1729 stays on node0 (cpu0, cpu1) then moves to node2 (cpu4);
  // thread 1730 starts on node2 (cpu4) then moves to node0 (cpu0, cpu1) -
  // one migration each, landing in different iterations.
  const std::vector<unsigned long long> expected_migrations{0, 1, 2};
  // numa_maps dominant node flips node0 -> node2 -> node0 across iterations.
  const std::vector<unsigned long long> expected_mem_migrations{0, 1, 2};

  std::map<std::string, unsigned long long> store_stats;
  for (int iteration = 1; iteration <= iterationCount; ++iteration) {
    std::stringstream iteration_path{};
    iteration_path << cur_path << "/" << iteration;
    monitor.update_stats(fake_pids, iteration_path.str());
    store_stats = monitor.get_text_stats();

    ASSERT_EQ(store_stats["numa_nodes_used"], 2u);
    ASSERT_EQ(store_stats["numa_migrations"],
              expected_migrations[iteration - 1]);
    ASSERT_EQ(store_stats["numa_mem_nodes_used"], 2u);
    ASSERT_EQ(store_stats["numa_mem_migrations"],
              expected_mem_migrations[iteration - 1]);
  }
}

TEST(MemmonTest, MemmonValueTestFixed) {
  std::string cur_path = base_path + "drop";
  std::vector<pid_t> fake_pids = mother_pid;

  std::unique_ptr<Imonitor> monitor(
      registry::Registry<Imonitor>::create("memmon"));

  const int iterationCount = 3;

  std::map<std::string, unsigned long long> store_stats;
  for (int iteration = 1; iteration <= iterationCount; ++iteration) {
    std::stringstream iteration_path{};
    iteration_path << cur_path << "/" << iteration;
    monitor->update_stats(fake_pids, iteration_path.str());
    store_stats = monitor->get_text_stats();
    for (auto stat : store_stats) {
      if (iteration == 1) {
        ASSERT_EQ(stat.second, 5000);
      } else if (iteration == 2) {
        ASSERT_EQ(stat.second, 10000);
      } else {
        ASSERT_EQ(stat.second, 2000);
      }
    }
  }
}

TEST(NetmonTest, NetmonMonitonicityTestFixed) {
  std::string cur_path = base_path + "drop";
  std::vector<pid_t> fake_pids = mother_pid;

  std::unique_ptr<Imonitor> monitor(
      registry::Registry<Imonitor, std::vector<std::string>>::create(
          "netmon", std::vector<std::string>()));

  const int iterationCount = 3;

  std::map<std::string, unsigned long long> store_stats;
  for (int iteration = 1; iteration <= iterationCount; ++iteration) {
    std::stringstream iteration_path{};
    iteration_path << cur_path << "/" << iteration << "/net/";
    monitor->update_stats(fake_pids, iteration_path.str());
    store_stats = monitor->get_text_stats();
    for (auto stat : store_stats) {
      if (iteration == 1) {
        ASSERT_EQ(stat.second, 500000);
      } else if (iteration == 2) {
        ASSERT_EQ(stat.second, 1000000);
      } else {
        ASSERT_EQ(stat.second, 1000000);
      }
    }
  }
}

TEST(NvidiamonTest, NvidiamonValueTestFixed) {
  std::string cur_path = base_path + "drop";
  std::vector<pid_t> fake_pids = mother_pid;

  std::unique_ptr<Imonitor> monitor(
      registry::Registry<Imonitor>::create("nvidiamon"));

  const int iterationCount = 3;

  std::map<std::string, unsigned long long> store_stats;
  const unsigned int MB_to_KB = 1024;
  for (int iteration = 1; iteration <= iterationCount; ++iteration) {
    std::stringstream iteration_path{};
    iteration_path << cur_path << "/" << iteration << "/nvidia/smi";
    monitor->update_stats(fake_pids, iteration_path.str());
    store_stats = monitor->get_text_stats();
    if (iteration == 1) {
      ASSERT_EQ(store_stats["gpufbmem"], 50 * MB_to_KB);
      ASSERT_EQ(store_stats["gpusmpct"], 50);
      ASSERT_EQ(store_stats["gpumempct"], 50);
    } else if (iteration == 2) {
      ASSERT_EQ(store_stats["gpufbmem"], 100 * MB_to_KB);
      ASSERT_EQ(store_stats["gpusmpct"], 100);
      ASSERT_EQ(store_stats["gpumempct"], 100);
    } else {
      ASSERT_EQ(store_stats["gpufbmem"], 20 * MB_to_KB);
      ASSERT_EQ(store_stats["gpusmpct"], 0);
      ASSERT_EQ(store_stats["gpumempct"], 0);
    }
  }
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

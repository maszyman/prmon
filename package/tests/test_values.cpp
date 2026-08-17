#include <unistd.h>

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
#include "../src/numamon.h"
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
    for (auto stat : store_stats) {
      if (iteration == 1) {
        ASSERT_EQ(stat.second, (5000000 * 2) / sysconf(_SC_CLK_TCK));
      } else if (iteration == 2) {
        ASSERT_EQ(stat.second, (10000000 * 2) / sysconf(_SC_CLK_TCK));
      } else {
        ASSERT_EQ(stat.second, (10000000 * 2) / sysconf(_SC_CLK_TCK));
      }
    }
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

TEST(NumamonTest, NumamonTopologyTestFixed) {
  numamon monitor{};

  // No sysfs node information at all, so nothing can be measured
  ASSERT_EQ(monitor.load_topology(base_path + "drop/1"), 0u);
  ASSERT_EQ(monitor.is_valid(), false);

  // Two nodes, numbered 0 and 2, alongside the other sysfs entries that must
  // not be mistaken for nodes
  ASSERT_EQ(monitor.load_topology(base_path + "numa/1"), 2u);
  ASSERT_EQ(monitor.is_valid(), true);

  nlohmann::json hw_json;
  monitor.get_hardware_info(hw_json);
  ASSERT_EQ(hw_json["HW"]["numa"]["Nodes"], 2);
  ASSERT_EQ(hw_json["HW"]["numa"]["node0"]["CPUs"], "0-3");
  ASSERT_EQ(hw_json["HW"]["numa"]["node2"]["CPUs"], "4-7");
  ASSERT_EQ(hw_json["HW"]["numa"].count("node1"), 0u);
}

TEST(NumamonTest, NumamonLocalityTestFixed) {
  std::string cur_path = base_path + "numa";
  std::vector<pid_t> fake_pids = mother_pid;

  // A zero sampling interval makes numa_maps be read on every cycle
  numamon monitor{0};

  const int iterationCount = 3;
  // Both threads on node0, then one on each node, then both back on node0
  const std::vector<unsigned long long> expected_spread{1, 2, 1};
  // All memory local, then half of it local to each of the two threads, then
  // a quarter once the huge page mapping is weighted by its own page size
  const std::vector<unsigned long long> expected_local_pct{100, 50, 25};

  std::map<std::string, unsigned long long> store_stats;
  for (int iteration = 1; iteration <= iterationCount; ++iteration) {
    std::stringstream iteration_path{};
    iteration_path << cur_path << "/" << iteration;
    // Each iteration carries its own copy of the (unchanging) topology
    ASSERT_EQ(monitor.load_topology(iteration_path.str()), 2u);
    monitor.update_stats(fake_pids, iteration_path.str());
    store_stats = monitor.get_text_stats();
    ASSERT_EQ(store_stats["numa_cpu_spread"], expected_spread[iteration - 1]);
    ASSERT_EQ(store_stats["numa_mem_local_pct"],
              expected_local_pct[iteration - 1]);
  }
}

TEST(NumamonTest, NumamonCadenceTestFixed) {
  std::string cur_path = base_path + "numa";
  std::vector<pid_t> fake_pids = mother_pid;

  // A one second cadence, so the memory measurement is held in between while
  // the spread continues to be taken every cycle
  numamon monitor{1};
  ASSERT_EQ(monitor.load_topology(cur_path + "/1"), 2u);

  std::map<std::string, unsigned long long> store_stats;
  monitor.update_stats(fake_pids, cur_path + "/1");
  store_stats = monitor.get_text_stats();
  ASSERT_EQ(store_stats["numa_mem_local_pct"], 100u);

  // Within the cadence the memory value is held, but the spread still moves
  monitor.update_stats(fake_pids, cur_path + "/2");
  store_stats = monitor.get_text_stats();
  ASSERT_EQ(store_stats["numa_mem_local_pct"], 100u);
  ASSERT_EQ(store_stats["numa_cpu_spread"], 2u);

  // Once it has elapsed the memory is measured again, so the cadence is a
  // repeating one and not a single shot
  sleep(2);
  monitor.update_stats(fake_pids, cur_path + "/3");
  store_stats = monitor.get_text_stats();
  ASSERT_EQ(store_stats["numa_mem_local_pct"], 25u);
  ASSERT_EQ(store_stats["numa_cpu_spread"], 1u);
}

int main(int argc, char **argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

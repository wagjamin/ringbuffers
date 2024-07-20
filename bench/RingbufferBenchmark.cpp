#include <chrono>

#include "AtomicRingbuffer.hpp"
#include "MutexRingbuffer.hpp"
#include "benchmark/benchmark.h"

using namespace ringbuffers;

namespace {

/// Param 1: Capacity
/// Param 2: Num Partitions
/// Param 3: Num Readers & Writers
/// Param 4: Messages per Writer
void BM_MutexRingbuffer(benchmark::State& state) {
  const size_t capacity = state.range(0);
  const size_t num_partitions = state.range(1);
  const size_t num_readers = state.range(2);
  const size_t messages_per_writer = state.range(3);
  MutexRingbuffer<std::string> buf(capacity, num_partitions, num_readers);

  auto reader = [&] {
    size_t partition_id = 0;
    while (auto message = buf.Retrieve(partition_id)) {
      partition_id = (partition_id + 1) % num_partitions;
    }
  };

  auto writer = [&] {
    size_t partition_id = 0;
    for (size_t k = 0; k < messages_per_writer; ++k) {
      buf.Add(partition_id, "payload");
      partition_id = (partition_id + 1) % num_partitions;
    }
    buf.ProducerDone();
  };

  for (auto _ : state) {
    std::vector<std::thread> all_threads;
    all_threads.reserve(2 * num_readers);
    const auto start = std::chrono::steady_clock::now();
    // Start workers.
    for (size_t k = 0; k < num_readers; ++k) {
      all_threads.emplace_back(reader);
      all_threads.emplace_back(writer);
    }
    // Wait for all to finish.
    for (auto& worker : all_threads) {
      worker.join();
    }
    const auto stop = std::chrono::steady_clock::now();
    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    state.SetIterationTime(static_cast<double>(dur) / 1000);
    state.SetItemsProcessed(num_readers * messages_per_writer);
  }
}

/// Param 1: Capacity
/// Param 3: Num Readers & Writers
/// Param 4: Messages per Writer
void BM_AtomicRingbuffer(benchmark::State& state) {
  const size_t capacity = state.range(0);
  const size_t num_readers = state.range(1);
  const size_t messages_per_writer = state.range(2);
  AtomicRingbuffer<std::string> buf(capacity, num_readers);

  auto reader = [&] {
    while (auto message = buf.Retrieve()) {
    }
  };

  auto writer = [&] {
    for (size_t k = 0; k < messages_per_writer; ++k) {
      buf.Add("payload");
    }
    buf.ProducerDone();
  };

  for (auto _ : state) {
    std::vector<std::thread> all_threads;
    all_threads.reserve(2 * num_readers);
    const auto start = std::chrono::steady_clock::now();
    // Start workers.
    for (size_t k = 0; k < num_readers; ++k) {
      all_threads.emplace_back(reader);
      all_threads.emplace_back(writer);
    }
    // Wait for all to finish.
    for (auto& worker : all_threads) {
      worker.join();
    }
    const auto stop = std::chrono::steady_clock::now();
    const auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(stop - start).count();
    state.SetIterationTime(static_cast<double>(dur) / 1000);
    state.SetItemsProcessed(num_readers * messages_per_writer);
  }
}

const size_t STATIC_NUM_WRITERS = 8;
const size_t STATIC_NUM_CONNS = 4;
const size_t STATIC_NUM_MESSAGES = 1'000'000;

BENCHMARK(BM_MutexRingbuffer)
    // Small and large capacity
    ->Args({STATIC_NUM_WRITERS * 16, STATIC_NUM_CONNS, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    ->Args({STATIC_NUM_WRITERS * 128, STATIC_NUM_CONNS, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    ->Args({STATIC_NUM_WRITERS * 16, 1, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    ->Args({STATIC_NUM_WRITERS * 128, 1, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    // Manual time measurement
    ->UseManualTime();

BENCHMARK(BM_AtomicRingbuffer)
    // Small and large capacity
    ->Args({STATIC_NUM_WRITERS * 32, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    ->Args({STATIC_NUM_WRITERS * 128, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    ->Args({STATIC_NUM_WRITERS * 256, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    ->Args({STATIC_NUM_WRITERS * 512, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    ->Args({STATIC_NUM_WRITERS * 1024, STATIC_NUM_WRITERS, STATIC_NUM_MESSAGES})
    // Manual time measurement
    ->UseManualTime();

}  // namespace

BENCHMARK_MAIN();

#include "AtomicRingbuffer.hpp"

#include "gtest/gtest.h"

#include <unordered_set>

using namespace ringbuffers;

namespace {

/// Param 1: Capacity
/// Param 2: Num Threads (Reader/Writer)
/// Param 3: Messages Per Writer
using ParamT = std::tuple<size_t, size_t, size_t>;

class RingbufferTestT : public ::testing::TestWithParam<ParamT> {
 public:
  RingbufferTestT()
      : buf(std::get<0>(GetParam()), std::get<1>(GetParam())),
        num_threads(std::get<1>(GetParam())),
        messages_per_writer(std::get<2>(GetParam())) {}

  AtomicRingbuffer<std::string> buf;
  const size_t num_threads;
  const size_t messages_per_writer;
};

TEST_P(RingbufferTestT, test_multi_threaded_complex_message) {
  // Messages received per reader.
  std::vector<std::unordered_set<std::string>> found(num_threads);
  // Number of messages received per reader - needed as above collection is duplicate free.
  std::vector<size_t> num_messages(num_threads);
  auto reader = [&](size_t reader_idx) {
    std::unordered_set<std::string>& materialize = found[reader_idx];
    while (std::optional<std::string> msg = buf.Retrieve()) {
      materialize.emplace(std::move(*msg));
      num_messages[reader_idx]++;
    }
  };

  auto writer = [&](size_t writer_idx) {
    for (size_t k = 0; k < messages_per_writer; ++k) {
      buf.Add(std::to_string(writer_idx) + "_" + std::to_string(k));
    }
    buf.ProducerDone();
  };

  std::vector<std::thread> workers;
  workers.reserve(2 * num_threads);
  for (size_t k = 0; k < num_threads; ++k) {
    workers.emplace_back(reader, k);
    workers.emplace_back(writer, k);
  }
  for (auto& thread : workers) {
    thread.join();
  }

  // Check that all messages arrived.
  size_t total = 0;
  for (const auto count : num_messages) {
    total += count;
  }
  EXPECT_EQ(total, num_threads * messages_per_writer);

  // Check that every individual message arrived.
  for (size_t thread_idx = 0; thread_idx < num_threads; ++thread_idx) {
    for (size_t k = 0; k < messages_per_writer; ++k) {
      const std::string expected_message = std::to_string(thread_idx) + "_" + std::to_string(k);
      size_t occurrences = 0;
      for (auto& map : found) {
        if (map.contains(expected_message)) {
          occurrences++;
        }
      }
      EXPECT_EQ(occurrences, 1);
    }
  }
}

INSTANTIATE_TEST_SUITE_P(RingbufferTests, RingbufferTestT,
                         ::testing::Values(ParamT{1024, 8, 1'000}));

}  // namespace

#include "MutexRingbuffer.hpp"

#include "gtest/gtest.h"

#include <mutex>
#include <thread>
#include <unordered_set>

using namespace ringbuffers;

namespace {

using TestBuffer = MutexRingbuffer<std::string>;

/// A set of reader threads that drain a MutexRingbuffer and
/// materialize the serialized strings.
class DrainingReaders {
public:
  DrainingReaders(TestBuffer &buf_, size_t num_partitions_,
                  size_t readers_per_partition_)
      : num_chunks(num_partitions_), buf(buf_), num_partitions(num_partitions_),
        readers_per_partition(readers_per_partition_) {
    if (readers_per_partition == 0) {
      throw std::runtime_error("At least one reader per partition required.");
    }
  }

  /// Asynchronously start the readers.
  void Start() {
    threads.reserve(readers_per_partition * num_partitions);
    auto reader_thread = [&](size_t partition_idx) {
      // Read until the partition is drained.
      while (std::optional<std::string> msg = buf.Retrieve(partition_idx)) {
        std::unique_lock lock(reader_lock);
        num_chunks[partition_idx]++;
        found.insert(std::move(*msg));
      }
    };
    // Emplace the required number of readers for every partition. They will
    // be reading data until the partition is drained.
    for (size_t r = 0; r < readers_per_partition; ++r) {
      for (size_t p = 0; p < num_partitions; ++p) {
        threads.emplace_back(reader_thread, p);
      }
    }
  }

  /// Wait for all readers to be finished.
  void Await() {
    for (auto &reader : threads) {
      reader.join();
    }
  }

  /// How many chunks were read per partition?
  std::vector<size_t> num_chunks;
  /// The serialized chunks.
  std::unordered_set<std::string> found;

private:
  /// The buffer from which we read.
  TestBuffer &buf;
  /// How many partitions are there?
  size_t num_partitions;
  /// How many reader threads exist per partition?
  size_t readers_per_partition;
  /// The reader threads being spawned.
  std::vector<std::thread> threads;
  /// Lock around which the readers synchronize when storing data in the found
  /// set.
  std::mutex reader_lock;
};

TEST(SerializationBufferTest, single_threaded_single_partition) {
  for (size_t k = 1; k <= 20; ++k) {
    TestBuffer buf{20, 1, 1};
    // Add k elements.
    for (size_t j = 0; j < k; ++j) {
      buf.Add(0, std::to_string(j));
    }
    // Remove k/2 elements.
    for (size_t j = 0; j < (k / 2); ++j) {
      std::optional<std::string> val = buf.Retrieve(0);
      EXPECT_TRUE(val);
      EXPECT_LE(std::stoull(*val), 20);
    }
    // Add k/2 elements.
    for (size_t j = 0; j < (k / 2); ++j) {
      buf.Add(0, std::to_string(j));
    }
    // Remove k elements.
    for (size_t j = 0; j < k; ++j) {
      std::optional<std::string> val = buf.Retrieve(0);
      EXPECT_TRUE(val);
      EXPECT_LE(std::stoull(*val), 20);
    }
    // Mark the producer as done.
    buf.ProducerDone();
    // Now we return that there is no more data.
    for (size_t k = 0; k < 10; ++k) {
      EXPECT_FALSE(buf.Retrieve(0));
    }
  }
}

TEST(SerializationBufferTest, multi_threaded_single_partition) {
  TestBuffer buf{50, 1, 20};

  // 20 Writer threads writing 100 messages each.
  auto writer_thread = [&](size_t id) {
    for (size_t k = 0; k < 100; ++k) {
      buf.Add(0, std::to_string(id) + "_" + std::to_string(k));
    }
    buf.ProducerDone();
  };

  // 10 Reader threads - they should read all 2000 messages.
  DrainingReaders readers(buf, 1, 10);
  // Start the readers first as it leads to more interesting synchronization.
  readers.Start();

  // Now start the writers.
  std::vector<std::thread> threads;
  threads.reserve(20);
  for (size_t k = 0; k < 20; ++k) {
    threads.emplace_back(writer_thread, k);
  }

  // Wait for all threads to be done.
  readers.Await();
  for (auto &thread : threads) {
    thread.join();
  }

  // Ensure every message was found.
  EXPECT_EQ(readers.found.size(), 2000);
  for (size_t id = 0; id < 20; ++id) {
    for (size_t k = 0; k < 100; ++k) {
      EXPECT_TRUE(
          readers.found.contains(std::to_string(id) + "_" + std::to_string(k)));
    }
  }
}

/// Test that we cannot deadlock if we are at capacity, and all readers
/// are waiting on an empty partition.
TEST(SerializationBufferTest, deadlock_readers_empty_partition) {
  TestBuffer buf{50, 2, 1};

  for (size_t k = 0; k < 50; ++k) {
    // Add 50 messages to the first partition, the buffer is now filled.
    buf.Add(0, "0_" + std::to_string(k));
  }

  // A background thread cannot add another message to partition 0.
  std::atomic<bool> writer_0_advanced = false;
  std::thread writer_0{[&]() {
    buf.Add(0, "0_50");
    writer_0_advanced.store(true);
  }};

  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  EXPECT_FALSE(writer_0_advanced.load());

  // We now have a reader trying to read from partition 1.
  std::atomic<bool> reader_1_advanced = false;
  std::thread reader_1{[&]() {
    auto result = buf.Retrieve(1);
    EXPECT_TRUE(result.has_value());
    EXPECT_EQ(*result, "1_0");
    reader_1_advanced.store(true);
  }};

  // It can't make progress since there is no message.
  std::this_thread::sleep_for(std::chrono::milliseconds{10});
  EXPECT_FALSE(reader_1_advanced.load());

  // But we can add a message to partition 1, this unblocks reader 1.
  buf.Add(1, "1_0");
  while (!reader_1_advanced.load()) {
    std::this_thread::yield();
  }

  // We can also read from partition 0, unblocking the writer there.
  auto result = buf.Retrieve(0);
  EXPECT_TRUE(result.has_value());
  EXPECT_EQ(*result, "0_49");
  while (!writer_0_advanced.load()) {
    std::this_thread::yield();
  }

  writer_0.join();
  reader_1.join();

  buf.ProducerDone();
}

} // namespace

#pragma once

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <vector>

namespace ringbuffers {

/// A partitioned, heavyweight thread-safe ringbuffer with a std::mutex and
/// condition variables protecting the buffer.
///
/// The MutexRingbuffer exerts backpressure across all partitions.
/// I.e. the buffer for an empty partition might become blocked if all other
/// partitions are almost full. 
template <class PayloadT> class MutexRingbuffer {
public:
  using PayloadType = PayloadT;

  /// Constructor. Backpressure is exerted once the capacity is reached.
  MutexRingbuffer(size_t capacity, size_t num_partitions, size_t num_producers);
  /// Thread-safe. Add a new serialized payload to the ringbuffer.
  void Add(size_t partition_id, PayloadT payload);
  /// Mark one of the producers as done.
  void ProducerDone();
  /// Thread-safe. Retrieve a serialized payload from the ringbuffer.
  /// Returns std::nullopt once no more data can be read.
  std::optional<PayloadT> Retrieve(size_t partition_id);
  /// Get the number of partitions in this serialization buffer.
  size_t NumPartitions() const;

private:
  struct alignas(64) PartitionBuffer {
    /// Current index at which the next serialized chunk should be written.
    size_t write_idx = 0;
    /// Data being stored in the buffer.
    std::vector<PayloadT> data;
  };

  /// How many writers are finished already?
  size_t finish_count = 0;
  /// The total chunks currently stored in the partitioned buffer.
  size_t total_chunks = 0;
  /// The total chunk capacity across all buffers. Backpressure is exerted once
  /// this limit is reached.
  size_t capacity;
  /// How many producers are writing to the partitioned buffer?
  size_t num_producers;
  /// Mutex protecting the buffer.
  std::mutex mut;
  /// The actual buffers storing serialized data.
  std::vector<PartitionBuffer> buffers;
  /// Condition variables used to wake up readers/writers, respectively.
  std::vector<std::condition_variable> read_cvs;
  std::condition_variable write_cv;
};

template <class PayloadT>
MutexRingbuffer<PayloadT>::MutexRingbuffer(size_t capacity,
                                           size_t num_partitions,
                                           size_t num_producers)
    : buffers(num_partitions), capacity(capacity), num_producers(num_producers),
      read_cvs(num_partitions) {
  // Every partition gets as many slots as the total capacity. This is an upper
  // bound for the size of every partition and ensures that we never have to
  // resize any vectors.
  for (auto &buffer : buffers) {
    buffer.data.resize(capacity);
  }
}

template <class PayloadT>
size_t MutexRingbuffer<PayloadT>::NumPartitions() const {
  return buffers.size();
}

template <class PayloadT>
void MutexRingbuffer<PayloadT>::Add(size_t partition_id, PayloadT payload) {
  std::unique_lock lock(mut);
  // Wait until there is capacity to write, or if we are writing to an empty
  // partition. We need to allow writing to an empty partition since otherwise
  // we can deadlock. Imagine all readers waiting on an empty partition, and all
  // writers being at capacity.
  write_cv.wait(lock, [&]() {
    return total_chunks < capacity || buffers[partition_id].write_idx == 0;
  });
  total_chunks++;
  assert(partition_id < buffers.size());
  PartitionBuffer &buffer = buffers[partition_id];
  const size_t idx = buffer.write_idx++;
  buffer.data[idx] = std::move(payload);
  lock.unlock();
  // Wake up a reader for the respective partition.
  read_cvs[partition_id].notify_one();
}

template <class PayloadT> void MutexRingbuffer<PayloadT>::ProducerDone() {
  std::unique_lock lock(mut);
  const size_t done = ++finish_count;
  if (done == num_producers) {
    // No data will ever be written again - this was the last producer to
    // finish. Wake up all waiting readers.
    for (auto &read_cv : read_cvs) {
      read_cv.notify_all();
    }
  }
}

template <class PayloadT>
std::optional<PayloadT>
MutexRingbuffer<PayloadT>::Retrieve(size_t partition_id) {
  std::unique_lock lock(mut);
  PartitionBuffer &buffer = buffers[partition_id];
  // Wait until there is something to read or all writers are done.
  read_cvs[partition_id].wait(lock, [&]() {
    return buffer.write_idx > 0 || finish_count == num_producers;
  });
  if (buffer.write_idx == 0) {
    // All writers are done and the partition is empty, nothing will ever be
    // read again.
    assert(finish_count == num_producers);
    return std::nullopt;
  }
  const auto idx = --buffer.write_idx;
  total_chunks--;
  assert(idx < capacity);
  PayloadT res = std::move(buffer.data[idx]);
  lock.unlock();
  // Wake up one of the writers. We can't wake up one specifically for the
  // partition, since backpressure is exerted across partitions.
  write_cv.notify_one();
  return res;
}

} // namespace ringbuffers

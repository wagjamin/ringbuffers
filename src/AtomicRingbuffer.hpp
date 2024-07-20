#pragma once

#include <algorithm>
#include <atomic>
#include <chrono>
#include <optional>
#include <thread>
#include <vector>

namespace ringbuffers {

/// A thread-safe ringbuffer that only uses atomics to maximize message
/// throughput. Competitor to the MutexRingbuffer which uses much more heavy
/// synchronization primitives.
template <class PayloadT> class AtomicRingbuffer {
public:
  using PayloadType = PayloadT;

  AtomicRingbuffer(size_t capacity_, size_t num_producers_);

  /// Thread-safe. Add a new serialized payload to the ringbuffer.
  void Add(PayloadT msg);
  /// Mark one of the producers as done.
  void ProducerDone();

  /// Thread-safe. Retrieve a serialized payload from the ringbuffer.
  /// Returns std::nullopt once no more data can be read.
  std::optional<PayloadT> Retrieve();

private:
  /// Advance index in the ringbuffer.
  size_t Advance(size_t curr_idx, size_t delta = 1);

  /// Put all atomics on their own cache lines.
  std::atomic<size_t> write_inflight = 0;
  char __pad_1[56];

  std::atomic<size_t> write_committed = 0;
  char __pad_2[56];

  std::atomic<size_t> read_inflight = 0;
  char __pad_3[56];

  std::atomic<size_t> read_committed = 0;
  char __pad_4[56];

  std::atomic<size_t> producers_done = 0;
  char __pad_5[56];

  /// Shared read-only state. Doesn't all need to be on different cache lines.
  const size_t capacity;
  const size_t num_producers;

  struct alignas(64) Slot {
    PayloadT payload;
  };
  std::vector<Slot> slots;
};

namespace {
const size_t spinlock_attempts = 100;
} // namespace

template <class PayloadT>
AtomicRingbuffer<PayloadT>::AtomicRingbuffer(size_t capacity_,
                                             size_t num_producers_)
    : capacity(capacity_), num_producers(num_producers_), slots(capacity_) {}

template <class PayloadT>
size_t AtomicRingbuffer<PayloadT>::Advance(size_t curr_idx, size_t delta) {
  return (curr_idx + delta) % capacity;
}

template <class PayloadT> void AtomicRingbuffer<PayloadT>::Add(PayloadT msg) {
  // Check whether another message can be added.
  size_t loaded_write;
  size_t attempts = 0;
  size_t backoff = 1;
  while (true) {
    loaded_write = write_inflight.load();
    if (read_committed.load() == Advance(loaded_write)) {
      // Spin for a while, checking whether a slot becomes free.
      if (attempts++ > spinlock_attempts) {
        // Backoff, wait until more messages were read.
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff = std::min(2 * backoff, static_cast<size_t>(50));
      }
      // Retry.
      continue;
    }
    if (write_inflight.compare_exchange_weak(loaded_write,
                                             Advance(loaded_write))) {
      // We got a slot for writing, continue with the actual messaging.
      break;
    }
  }
  // We now have our index for writing. Store the message.
  const size_t new_write_idx = Advance(loaded_write);
  slots[loaded_write].payload = std::move(msg);
  // Now we need to publish the message.
  size_t expected = loaded_write;
  while (!write_committed.compare_exchange_weak(expected, new_write_idx)) {
    expected = loaded_write;
  }
}

template <class PayloadT> void AtomicRingbuffer<PayloadT>::ProducerDone() {
  producers_done.fetch_add(1);
}

template <class PayloadT>
std::optional<PayloadT> AtomicRingbuffer<PayloadT>::Retrieve() {
  // Check whether a message can be retrieved.
  size_t loaded_read;
  size_t attempts = 0;
  size_t backoff = 1;
  while (true) {
    loaded_read = read_inflight.load();
    if (write_committed.load() == loaded_read) {
      if (producers_done.load() == num_producers) {
        // No more messages will arrive. We are done.
        return std::nullopt;
      }
      // Spin for a while, checking whether a slot becomes free.
      if (attempts++ > spinlock_attempts) {
        // Backoff, wait until more messages were added.
        std::this_thread::sleep_for(std::chrono::milliseconds(backoff));
        backoff = std::min(2 * backoff, static_cast<size_t>(50));
      }
      // Retry.
      continue;
    }
    if (read_inflight.compare_exchange_weak(loaded_read,
                                            Advance(loaded_read))) {
      // We got a slot for reading, continue with the actual messaging.
      break;
    }
  }
  // Load the message.
  PayloadT res = std::move(slots[loaded_read].payload);
  // Publish that the message was read.
  size_t expected = loaded_read;
  while (!read_committed.compare_exchange_weak(expected, Advance(expected))) {
    expected = loaded_read;
  }
  return res;
}

} // namespace ringbuffers

## Ringbuffers

This repository contains a header-only implementation of two C++ ringbuffers. 
- `AtomicRingbuffer` is a fixed-capacity, non-partitioned ringbuffer that only uses atomics
- `MutexRingbuffer` is a fixed-capacity, partitioned ringbuffer that uses heavyweight mutexes and condition variables

Unit tests are in `test`, a simple microbenchmark that does concurrent reading and writing is in `bench`.

Both implementations can probably still be tuned a lot. I initially built them when I was
prototyping shuffle implementations for a query engine. This is where the partitioned ringbuffer becomes useful:
you might need to send packets to different nodes and can use the partition ID to hand over work between
query processing and network workers.

Note that the `AtomicRingbuffer` has a very primitive backup policy where readers and writers sleep for a while if
they can't make progress. If you want readers and writers to wake up really quickly, you should to go with the `MutexRingbuffer`.

Our microbenchmarks on an i7-10700 show that with eight writers and eight readers, the `MutexRingbuffer` can handle ~2M messages per second.
The `AtomicRingbuffer` can achieve a throughput of up to ~7M messages per second.


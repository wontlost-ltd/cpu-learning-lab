# Search benchmark

```sh
make benchmark
python3 benchmarks/collect.py build/search-benchmark --output /tmp/cpuai-benchmark.json
```

Or configure CMake with `-DCPUAI_BUILD_BENCHMARKS=ON` and run `search-benchmark`.

Record the actual compile flags with collected results; the collector cannot infer them from an executable. The committed sample was compiled with g++ C++20 and `-O2 -Wall -Wextra -Wpedantic`.

The native executable runs 200 iterations of:

- Cold: construct a fresh session and fit three affine lessons, then predict an unseen input.
- Warm: query the same completed learner, then check the same prediction.
- Incremental: prime a fresh learner with 0→0 and 1→1 outside the timer; time fitting the appended lesson 2→4 and check square(3).

Output reports p50/p95 microseconds, correctness checks, total new combinations, numeric example checks, current graph/index requested bytes and peak requested bytes. Warm/cold measure search only; external prediction checks are outside their timer. Incremental setup is outside its timer. These are distinct workloads and should not be interpreted as comparable full-training times.

The committed sample result also records CPU model, visible logical CPU count, compiler/flags, OS and process maximum RSS. The Linux collector uses resource.getrusage(RUSAGE_CHILDREN) immediately after running the benchmark, before spawning other children; ru_maxrss is KiB on Linux. This process peak covers all benchmark phases, not just the graph. PMR requested-byte accounting excludes allocator metadata and all non-graph allocations.

Results come from a shared execution environment, not a dedicated or battery-powered device. They show measured local behavior, not an energy reduction or a cross-hardware speed guarantee. No pass/fail threshold depends on microsecond timing. Reproduce and compare quality and work counters alongside latency.

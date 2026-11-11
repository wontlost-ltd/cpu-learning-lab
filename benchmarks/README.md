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

## P4 音频管线

```sh
make build/audio-benchmark
./build/audio-benchmark /path/to/mono-pcm16.wav
```

或用 CMake 的 `-DCPUAI_BUILD_BENCHMARKS=ON`。程序预热一次，再测 30 次 WAV 读取、固定特征提取和单标签最近邻查询，输出 p50/p95（毫秒）。重复同一文件，包含热缓存影响；不含录音设备和传输，非硬实时保证，非能耗测量。边界与先验见 ADR 0005。

## P4 连续输入基准

```sh
python3 benchmarks/stream_collect.py build/cpu-listen --output stream-results.json
```

脚本用确定性音调，经快速 PCM 管道测试 100 个 100 ms / 16 kHz 窗口和 20 个 2 s / 48 kHz 窗口；未教学模型，不保存录音。输出处理 p50/p95 与完整进程墙钟时间。窗口等待、麦克风驱动、stdout 与录制写入不计入 processing_ms；不能当成硬件端到端延迟或功耗。当前记录见 `results/p4-stream-linux-2026-11-11.json`。

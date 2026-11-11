# 项目结构与边界

## 已实现的仓库结构

| 路径 | 职责 |
| --- | --- |
| `include/cpuai/durable_file.hpp` | 原生独占文件、完整写入、平台同步与尾截断 |
| `include/cpuai/device_sync.hpp` | 稳定事件身份、CRC 帧、恢复、冲突检测、回执与幂等同步 |
| `apps/sync/main.cpp` | 同机文件节点初始化、显式经验记录、状态和补传 |
| `tests/unit/device_sync_test.cpp` | 接收／回执中断、旧快照补传、尾损坏与资源边界 |
| `tests/integration/sync_cli_test.py` | 原生跨进程重启、生产者强制结束、持久确认与补传 |
| `include/cpuai/audio_stream.hpp` | 有界连续帧、消费确认、窗口切片／取消、暂停和断流片段、WAV 录制 |
| `adapters/audio/` | 可停止 PCM 管道输入与可选 PortAudio 默认麦克风 |
| `apps/listen/main.cpp` | 连续识别、有限捕获、显式反馈快照加载、JSON Lines |
| `tests/unit/audio_stream_test.cpp` | 分块等价、背压重试、断点隔离、暂停／续算与停止 |
| `tests/integration/listen_cli_test.py` | 连续进程、录音教学、修订替换、损坏更新与输入边界 |
| `include/cpuai/observations.hpp` | 有界观察流、来源水位、背压、显式时钟误差对齐 |
| `include/cpuai/audio.hpp` | PCM16 WAV 解码、固定信号特征、用户教学分类、持久化、独立考试 |
| `tests/unit/observations_test.cpp` | 重复、迟到、满队列重试、时间误差与来源限制 |
| `tests/integration/audio_cli_test.py` | 原生音频教学／纠错／留出测试／重启及损坏输入 |
| `include/cpuai/learner.hpp` | 学习 API、修订和示例存储；header-only |
| `include/cpuai/polynomial.hpp` | 精确多项式身份、数值执行和域检查 |
| `include/cpuai/runtime.hpp` | 控制令牌、截止时间、PMR 内存预算与计量 |
| `include/cpuai/session.hpp` | DAG、可续算枚举、候选匹配 |
| `include/cpuai/search.hpp` | 结果生成、共识预测和主动问题 |
| `include/cpuai/skills.hpp` | 已验收版本、固定依赖、组合搜索、重复子流程提取、回放与恢复 |
| `tests/unit/skills_test.cpp` | 新任务组合、抽象、版本回滚、失效候选与持久化 |
| `include/cpuai/evaluation.hpp` | 冻结候选、独立测试及报告有效性检查 |
| `tests/unit/runtime_test.cpp` | 缓存、修正、DAG、暂停、取消和资源预算 |
| `benchmarks/` | 原生冷热／增量查询基准及环境记录 |
| `tests/evaluation/` | 固定未见输入回归样例 |
| `tests/integration/` | CLI 独立进程及恢复测试 |
| `apps/cli/main.cpp` | 教学、查询、保存加载命令；不持有领域答案 |
| `tests/unit/learner_test.cpp` | 可运行的学习行为、泛化、纠错和存储测试 |
| `examples/` | 可复现教学会话 |
| `docs/architecture.md` | 目标架构、数据流与 CPU 约束 |
| `docs/roadmap.md` | 分阶段目标与验收标准 |
| `docs/adr/` | 技术决策及其适用范围 |
| `.github/workflows/ci.yml` | Linux、macOS、Windows 构建测试配置 |
| `CMakeLists.txt` / `Makefile` | 跨平台构建入口／轻量本地构建入口 |

## 随实现逐步引入的模块

以下是目标分层，不创建空壳并声称能力已存在。

| 未来路径 | 职责与边界 |
| --- | --- |
| `include/cpuai/contracts/` | Observation、Lesson、Hypothesis、Skill、Evaluation、Budget 的版本化契约 |
| `src/core/` | 学习状态、概念与技能 ID；不依赖设备、网络或界面 |
| `src/learning/` | 候选归纳、多假设、主动求教、在线更新、巩固 |
| `src/runtime/` | 受限技能解释器、任务调度、取消和资源计量 |
| `src/memory/` | 经验、来源、反例、版本、索引和遗忘策略 |
| `src/evaluation/` | 生产评估执行器；不生成本次学习材料 |
| `src/compute/` | CPU 数值／SIMD 后端及平台能力探测 |
| `adapters/perception/` | 音视频和传感器编码器；声明先验及模型来源 |
| `adapters/chronosynth/` | 领域契约映射，只有实际验收后才能输出能力事件 |
| `adapters/devices/` | 手机、眼镜、手表平台采集与传输 |
| `tests/integration/` | 独立进程恢复、升级、教学闭环测试 |
| `tests/evaluation/` | 冻结的未见任务、反例、组合能力与遗忘检查 |
| `benchmarks/` | 时间、内存、能耗实验及硬件环境记录 |

依赖方向：apps/adapters → public contracts/runtime → learning/core/memory；core 不反向依赖适配器。evaluation 的正确答案不得流入候选生成器。

P1 已提取多项式表示、搜索与评估；存储暂留 learner.hpp。未来的 compiled source 模块按性能和稳定性需要引入，避免过早创建大量空包。

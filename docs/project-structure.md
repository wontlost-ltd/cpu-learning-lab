# 项目结构与边界

## 已实现的仓库结构

| 路径 | 职责 |
| --- | --- |
| `include/cpuai/learner.hpp` | 学习 API、修订和示例存储；header-only |
| `include/cpuai/polynomial.hpp` | 精确多项式身份、数值执行和域检查 |
| `include/cpuai/search.hpp` | 有界多假设搜索、共识预测和主动问题 |
| `include/cpuai/evaluation.hpp` | 冻结候选、独立测试及报告有效性检查 |
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

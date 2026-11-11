# CPU Learning Lab — 原生持续学习原型

C++20、CPU-only、无第三方运行时依赖、无 LLM/API 调用。当前阶段：P2。

用户提供输入输出示例，系统归纳可执行算术假设，保留歧义并主动求教。它是有明确先验和边界的程序归纳实验，尚不是通用白板智能。

## 项目导航

- [目录规划](docs/project-structure.md)
- [目标架构](docs/architecture.md)
- [路线图与验收](docs/roadmap.md)
- [P1 设计与边界](docs/adr/0002-hypotheses-and-evaluation.md)
- [P2 CPU 运行时与资源边界](docs/adr/0003-cpu-runtime.md)
- [可复现基准](benchmarks/README.md)
- [贡献说明](CONTRIBUTING.md)

## 构建与测试

```sh
make all test
./build/cpu-learn
```

需要 C++20 编译器；测试驱动使用 Python 3 标准库，原生应用运行不需要 Python。

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

仅构建原生应用，可配置 `-DBUILD_TESTING=OFF`。Windows 多配置构建的程序位于 `build/Release/cpu-learn.exe`。

## 教学、歧义和主动求教

在项目根目录运行：

```text
teach square 0 0
teach square 1 1
show square
ask square 2
next square
exam square tests/evaluation/square-v1.exam
teach square 2 4
ask square 3
exam square tests/evaluation/square-v1.exam
save learner.state
quit
```

前两个示例同时符合 x、x² 等 6 个不同多项式。`ask square 2` 返回不确定，`next square` 建议在 x=-3 处教学。示例中也可手动提供另一项有效教学 `2→4`，从而收敛到平方规则。

第一次独立测试失败，补充教学后通过；`exam` 在读取测试文件之前冻结最简单的保留候选，评分不会筛选候选或写回教学数据。

```text
load learner.state
ask square 4
```

新进程恢复后应输出 16。支持双引号括起含空格的名称或文件路径；同一输入再次教学会替换旧标签。

## 学习能力与 CPU 边界

- 基本元素 x、整数 -3..3、加减乘；默认搜索完整的两层表达式空间，不在第一个拟合规则处提前结束。
- 按精确整数多项式系数去重，不按教学输出去重。相同函数只留一个表达式代表；x 和 x² 不会因为两个样本相同而合并。
- 默认最多 50,000 次组合、4,096 个唯一表达式、64 个候选，以及 4 MiB 的 DAG／索引申请字节数。达到搜索或候选限制会明确标记不完整，拒绝给出确定预测。
- `ask` 要求所有保留候选在该输入上数值一致；这只是限定语法中的共识，不是领域能力认证。
- `next` 在有限探测网格中按最坏剩余候选数选择问题，跳过已教输入；无分歧问题不等于学会。
- `exam` 要求 3..128 个独立输入，拒绝与教学重叠或重复的测试点。报告绑定候选、教学快照、修订号、搜索配置及测试集 ID。
- `passed_on_suite` 仅表示该候选在该测试集全对且搜索完整，不代表所有候选通过或一般性证明；歧义仍由 `ask/next` 单独处理。
- 每个技能最多 128 个示例、最多 64 个技能；保持 CPUAI1 状态格式兼容。恢复后重新归纳，不持久化能力认证。

## P2：资源控制与增量缓存

```text
show square
stats
show square
stats
cancel-next
ask square 4
stats
ask square 4
```

`stats` 显示最近一次 `show/ask/next` 的组合数、样本检查次数、缓存命中、增量筛选、DAG 申请字节数和耗时。第二次相同查询无需重新生成规则或检查样本；新增示例只筛选旧候选，修改已有标签会重新筛选完整 DAG。索引和 DAG 共享于该实例的各技能，最近一次拟合缓存只有一份。

- `limits OPS MS BYTES`：组合总预算、每次查询的协作截止时间（毫秒）、DAG/索引字节预算。例如 `limits 50000 1000 4194304`。MS 最大 60000，0 表示立即截止。
- `slice OPS`：每次最多生成多少组合；重复查询可继续未完成会话。`slice 50000` 可恢复默认规模的整次搜索；`slice 0` 只暂停生成。
- `cancel-next`：演示下一次搜索取消。C++ API 接受 `std::stop_token`，调用者可在执行中请求取消。
- 超时和取消保留已生成 DAG 及游标，下次可继续；达到组合、节点或内存总上限则该配置停止，需换预算重建。

截止时间为协作检查，不会强行终止线程。只约束生成／匹配中的检查点，不保证返回格式化、分配器或 OS 调度的硬实时。PMR 上限只覆盖 DAG 向量与索引申请字节数（包含扩容时旧新缓冲区重叠），不覆盖缓存、返回结果、栈、分配器元数据或进程 RSS。

`freeze` 支持传入相同资源控制；不完整搜索仍不能通过能力验收。`load` 会丢弃旧会话缓存并重建，文件只保存教学经验。

## 评估纪律

测试标签只进入评估器，不进入搜索。测试失败不能自动作为反例教学。反复查看同一个测试集后据其调整教学会造成评估污染；这些公开固定样例只是回归测试，不是盲测证据。

学习更新会使旧修订的报告失效，API 使用 `isCurrent` 检查。报告仍可作为历史评估记录。修订号在重启后重建，不是全局内容身份；跨设备持久认证属于后续工作。

## 已执行验证

`make test` 包含运行时／学习单元测试、独立评估测试和 CLI 进程测试。覆盖歧义保留、主动问题、反例收敛、预算／容量截断、未见输入、考试污染拒绝、报告修订失效、持久化和重启。

GitHub 配置 Linux/macOS/Windows CI；历史运行曾因账户付款／消费额度设置未启动，不能把本地通过说成三平台已通过。

## 尚未实现

概念创造、跨技能组合、梯度训练、音视频学习、硬实时保证、系统级内存隔离和断电原子保存。已实现 DAG、会话内暂停续算、缓存及增量筛选，但缓存与搜索游标不持久化；内核实例为单调用者设计。未测量能耗，也没有证明优于 GPU 或其他语言。

下一阶段 P3：已学技能组合、抽象提取、版本回滚和巩固回放。源码未指定开源许可证，公开或分发前由项目所有者决定。

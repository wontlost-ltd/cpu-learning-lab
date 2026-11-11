# CPU Learning Lab — 原生持续学习原型

独立实验项目，C++20、CPU-only、无第三方依赖、无 LLM/API 调用。

目标：验证用户给出的输入输出示例能改变持久能力，而不只是生成“我学会了”的叙事。

## 项目导航

- [目录规划](docs/project-structure.md)
- [目标架构](docs/architecture.md)
- [路线图与验收](docs/roadmap.md)
- [技术决策](docs/adr/0001-learning-core.md)
- [贡献说明](CONTRIBUTING.md)

## 构建

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/cpu-learn
```

也可以直接构建：

```sh
g++ -std=c++20 -O2 -Iinclude apps/cli/main.cpp -o cpu-learn
g++ -std=c++20 -O2 -Iinclude tests/unit/learner_test.cpp -o learning-tests
./learning-tests
```

## 教学与重启

```text
teach transform -2 -3
teach transform 0 1
teach transform 3 7
show transform
ask transform 9
save learner.state
quit
```

新进程中输入：

```text
load learner.state
ask transform 11
```

应输出 23。规则由示例搜索产生，任务的答案不是预先写入。`teach` 对同一输入再次教学会替换旧标签；不同技能的数据独立。技能名称使用不含空格的标识符。

## 第一版实现

- 表达式归纳：预设基本元素 x、整数 -3..3、加减乘；搜索至多 3 层。
- 每次推断至多评估 50,000 个组合，预算不足明确报告失败。
- 相同训练输出向量只保留一个表达式代表，减少搜索。
- 首次找到一致表达式层即返回；这是简单性偏好，不是统计置信度。
- 至少 3 个示例才提供预测；这个数量是产品门槛，不是正确性保证。
- 每个技能最多 128 个示例；加载文件最多 64 个技能。
- 自有算术 DSL 解释器，不执行系统命令或用户代码。
- 保存版本化示例，加载后重新归纳；不是序列化模型二进制。

## 必须明确的局限

这是有限语法中的符号程序归纳原型，不是通用白板智能、神经网络或数学专家。
搜索语言由开发者预设；还没有概念创造、跨技能组合、感知编码器或梯度训练。
同训练输出不代表同函数：按样本签名去重会遗漏在新输入上不同的假设。
目前选一个代表假设，不能完整量化歧义或保证主动询问找到全部反例。
增加示例后会从头搜索，没有实现真正的增量搜索缓存。
候选数和表达式深度有界，但没有硬墙钟期限或系统级内存隔离。
保存为直接文件写入，尚不具备断电原子性；仅适合本地实验。
尚未测量能耗，也没有证明优于其他语言或 GPU 系统。

## 已执行验证

使用 g++ C++20、-O2、-Wall、-Wextra、-Wpedantic 编译通过。
测试覆盖未见输入泛化、反例促使假设修改、保存加载、技能隔离、搜索预算和非有限输入拒绝。
测试是功能证据，不代表任意函数可学。测试见 tests/unit/learner_test.cpp。

## 下一阶段

1. 保留有界的多假设集合，用分歧选择下一项教学问题。
2. 冻结独立评估任务，区分记录、拟合、泛化与可组合能力。
3. 增量搜索、表达式 DAG、缓存与 wall-time/内存预算。
4. 可复用技能调用及反例回归；错误新知识不影响已验证版本。
5. 观察事件接口，再接音视频/传感器适配；明确感知先验来源。
6. 如接 ChronoSynth，只有独立执行评估达标后才发 capability-learned。

源码未指定开源许可证：公开或分发前由项目所有者决定。

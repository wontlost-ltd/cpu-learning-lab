# CPU Learning Lab — 原生持续学习原型

C++20、CPU-only、默认构建无第三方运行时依赖、无 LLM/API 调用。当前阶段：P5 iOS/Apple Watch 前台采集、持久中继和配对 TLS；P4 真实设备和功耗验收仍待完成。

用户提供输入输出示例，系统归纳可执行算术假设，保留歧义并主动求教。它是有明确先验和边界的程序归纳实验，尚不是通用白板智能。

## 项目导航

- [目录规划](docs/project-structure.md)
- [目标架构](docs/architecture.md)
- [路线图与验收](docs/roadmap.md)
- [P1 设计与边界](docs/adr/0002-hypotheses-and-evaluation.md)
- [P2 CPU 运行时与资源边界](docs/adr/0003-cpu-runtime.md)
- [可复现基准](benchmarks/README.md)
- [P3 技能组合与版本设计](docs/adr/0004-skill-composition.md)
- [P4 音频与观察流设计](docs/adr/0005-observations-and-audio.md)
- [P4 连续音频与反馈设计](docs/adr/0006-continuous-audio.md)
- [连续音频操作示例](examples/live-audio.md)
- [本地声音教学示例](examples/audio-session.md)
- [P5 持久事件与同步设计](docs/adr/0007-durable-device-events.md)
- [P5 配对与 TLS 设计](docs/adr/0008-paired-tls-transport.md)
- [iOS 原生采集端](mobile/ios/README.md)
- [Apple Watch 伴侣采集](mobile/ios/WatchApp/README.md)
- [Watch 持久中继设计](docs/adr/0010-watch-collector-relay.md)
- [iOS 采集设计与边界](docs/adr/0009-ios-collector.md)
- [网络补传示例](examples/network-sync.md)
- [离线补传示例](examples/device-sync.md)
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

## P3：从已学技能组合新能力

完整演示：

```sh
./build/cpu-learn < examples/skill-composition.session
```

先分别教会并独立验收 `shift(x)=x+1` 与 `square(x)=x²`，再给新任务 `fourth` 三个示例 `0→1、1→16、2→81`。默认两层原语搜索没有拟合规则；组合搜索会找到 `shift@1 → square@2 → square@2`，并在新输入 4 上返回 625。这些调用顺序由目标示例筛选产生，验收答案不参与选择。

- `publish NAME EXAM`：从教学草稿冻结唯一且搜索完整的规则；独立验收及旧技能回放通过后发布。
- `plan NAME` / `compose NAME EXAM`：搜索已发布技能的 1..3 步组合。默认最多 512 个程序、64 个代数候选；超预算、取消或歧义不能发布。
- `run NAME X`：调用已发布的具体版本。`ask` 仍查询教学草稿，两者明确分开。
- `versions NAME` / `rollback NAME ID`：查看历史并切换已验收版本；不覆盖历史，也不修改教学草稿。
- `mine`：从不同已发布流程中寻找重复的 2..3 步子流程；会展开依赖到叶技能。
- `abstract NAME INDEX EXAM`：把选中的重复流程提取为可复用技能。必须先给目标教学示例，再做独立验收。INDEX 是当前 `mine` 的零起始序号。
- `replay`：检查当前已发布技能各自保存的验收集，不把答案写回教学数据；最多 1,000,000 次技能调用。
- `save-library PATH` / `load-library PATH`：保存或恢复技能历史、固定依赖、验收集和活动版本。与教学经验的 `save/load` 文件分开。

同名技能升级不会改变旧组合：依赖绑定名称和版本 ID。例如将 shift 升级为 x+2 后，已发布的 fourth 仍使用 shift@1。激活新版本前检查其他活动技能；同名任务本身允许通过新的教学和验收有意改变定义。

技能库最多保存 256 个版本、64 个活动名称；每个流程最多 3 步、递归深度 8、展开调用成本 64。运行中检查中间输入范围与输出范围，超出范围拒绝执行。组合按多项式身份合并候选，但不同调用路径可能有不同的中间值适用范围，因此验收还要运行真正的固定依赖流程，不能只验算扁平公式。

恢复时检查版本顺序、依赖、公式、教学和验收结果，拒绝损坏文件；原语规则重新归纳验证。档案最多 4 MiB，格式为 `CPUAI_LIBRARY1`。这是本地可验证记录，不是加密签名或跨设备全局身份。重载或移动技能库使未提交候选失效。文件写入尚不具备断电原子性。

## 评估纪律

测试标签只进入评估器，不进入搜索。测试失败不能自动作为反例教学。反复查看同一个测试集后据其调整教学会造成评估污染；这些公开固定样例只是回归测试，不是盲测证据。

学习更新会使旧修订的报告失效，API 使用 `isCurrent` 检查。报告仍可作为历史评估记录。修订号在重启后重建，不是全局内容身份；跨设备持久认证属于后续工作。

## 已执行验证

`make test` 包含运行时、学习、技能版本单元测试，独立评估测试，以及两组 CLI 进程测试。覆盖歧义保留、主动问题、反例收敛、预算／容量截断、未见输入、考试污染拒绝、报告修订失效、持久化和重启。

GitHub 配置 Linux/macOS/Windows CI；历史运行曾因账户付款／消费额度设置未启动，不能把本地通过说成三平台已通过。

## 尚未实现

开放式概念创造、梯度训练、音视频学习、硬实时保证、系统级内存隔离和断电原子保存。P3 的抽象是从重复调用流程提取可复用程序，不是发明任意新概念；回放是回归验收，不是神经训练。已实现 DAG、会话内暂停续算、缓存及增量筛选，但缓存与搜索游标不持久化；内核实例为单调用者设计。未测量能耗，也没有证明优于 GPU 或其他语言。

下一阶段 P4：观察事件流、时间对齐与首个感知适配器。源码未指定开源许可证，公开或分发前由项目所有者决定。

## P4：本地声音教学与观察流

`audio-teach ID LABEL WAV` 增量记录用户提供的声音标签；同 ID 可纠错。`audio-ask WAV` 使用有界最近邻分类，距离远或标签冲突时返回未知；`audio-save/load PATH` 保存恢复。没有预训练编码器，当前特征是人工选定的 RMS、过零率与八个频率探针，适合先做简单声音实验，不能宣称能理解任意声音。

`audio-exam MANIFEST` 冻结当前模型后评价 3..128 条独立样本，拒绝与教学相同的 PCM，不把测试答案写回学习。CLI 报告测试集范围内的 correct/unknown 与模型修订。

`observe SOURCE SEQ TIME OFFSET ERROR WAV` 接收有时间信息的音频特征事件；`align A B MS` 按显式时钟偏移与误差上界对齐。默认缓冲 64 条，满时明确拒绝并允许排空后重试。`stream-pop` 取出最早事件；`stream-stats` 查看接受、拒绝与峰值。事件缓冲仅存在于当前进程。

WAV 限单声道 PCM16，8..48 kHz、50 ms..2 s。音频路径有输入大小与样本数上限，尚不支持 `limits`/取消令牌。已增加可选麦克风入口（见下节），尚无语音识别、视觉、自动时钟同步或穿戴设备驱动。真实录音泛化、持续输入调度和功耗仍需后续实测。详见[教学示例](examples/audio-session.md)与[设计边界](docs/adr/0005-observations-and-audio.md)。

## 连续输入与显式用户反馈

新增原生 `cpu-listen`：

```sh
./build/cpu-listen --stdin 16000 100 sounds.state 1000 < mono-16k.pcm
```

按有界窗口处理 PCM，输出带模型修订与片段／帧位置的 JSON Lines。支持精确消费帧确认、背压重试、切片与取消；暂停、续算和设备断流的语义由单元测试覆盖。默认不保存音频，显式 `--capture NEW_DIR MAX_FILES` 才录制有限窗口供用户教学。

可选 `-DCPUAI_WITH_PORTAUDIO=ON` 构建连接系统默认麦克风（另需安装 PortAudio）；使用 `--microphone` 代替 `--stdin`。`--reload` 允许用户在另一进程纠错并保存模型，监听进程在窗口边界采用新的完整快照，损坏更新不会替换当前模型。实时加载不是独立验收认证，也没有把预测当作教学标签。

完整命令、权限、构建与计量边界见[连续音频示例](examples/live-audio.md)和[ADR 0006](docs/adr/0006-continuous-audio.md)。真实麦克风、噪声泛化、跨设备时钟和能耗仍待实测。

## P5 基础：持久事件与幂等补传

原生 `cpu-sync` 为音频特征经验提供有界日志与同机文件同步。稳定身份由设备、会话和序号组成；相同身份的相同内容去重，不同内容报冲突。完整帧写入并完成系统同步后才确认；不完整尾写入可恢复，完整损坏不会被自动删除。

`cpu-listen ... --journal PATH DEVICE` 显式启用每窗口持久化，输出 durable_event；默认不写日志。接收端提交后回执中断可以重试而不重复插入，接收端恢复旧快照后也会检查实际持有的事件并补传。日志限 4096 事件／64 来源／8 目标／16 MiB，满后拒绝，不静默淘汰。

当前没有手机／穿戴应用或网络服务；监听和同步不能同时打开同一日志。用户标签事件只保存、复制，不自动教学，也不触发 ChronoSynth 学会事件。强制同步有额外 I/O 和功耗成本，尚未实测。操作见[补传示例](examples/device-sync.md)，可靠性范围见 [ADR 0007](docs/adr/0007-durable-device-events.md)。

## P5 网络步骤：配对、鉴权与 TLS 补传

可选 `CPUAI_WITH_NETWORK=ON` 构建原生 cpu-link（macOS/Linux，OpenSSL 3）。操作者离线授权设备，配对包绑定接收端证书指纹、设备 ID 和独立密钥；客户端先验证证书再发送密钥，服务端逐批检查授权与事件来源，支持撤销和密钥轮换。

真实 TCP/TLS 传输沿用持久事件确认和幂等重试。单批最多 128 条，单连接最多 32 批，长度与收发期限有界；提交后断线可重试。默认只监听回环地址，尚无远程自动注册、移动 App 或生产级网络服务。默认构建继续不依赖 OpenSSL。

详见[构建与配对操作](examples/network-sync.md)和 [ADR 0008](docs/adr/0008-paired-tls-transport.md)。下一步仍按原路线接手机采集端、穿戴扩展和由执行评估驱动的 ChronoSynth 适配，视频不在本阶段新增。

## P5 手机步骤：iOS 原生前台采集

`mobile/ios` 提供 SwiftUI 应用、Swift Package 与可复现 XcodeGen 工程。音频回调只写有界 C++ 队列，后台复用同一特征提取和持久日志；配对凭据存入 Keychain，用户停止采集后显式通过 Apple TLS 客户端补传。iOS 不依赖 OpenSSL。

默认不录音、不联网；未配对不能开始。后台、音频中断或路由变更时停止，不自动恢复。只保存／发送特征，暂不提供手机端模型教学／识别或学会事件。构建、真机签名和 iPhone→Mac 操作见 [iOS README](mobile/ios/README.md)。模拟器验证不能替代真实麦克风、噪声和功耗验收。

## P5 穿戴步骤：Watch → iPhone → Mac

Apple Watch 伴侣 App 复用同一 C++ 音频队列和日志，默认不采集，只在用户确认配置并授权后前台录入特征。手动 WatchConnectivity 补传最多 32 条／32 KiB，iPhone 先持久化再回执；手表保留原件，随后 iPhone 用独立 watch 凭据向 Mac 补传。

系统配对关系是中继信任边界，密钥只留在 iPhone Keychain；没有硬件身份证明或后台传输承诺。iPhone ACK 不等于 Mac ACK，更不等于模型学会。构建与操作见 [Watch README](mobile/ios/WatchApp/README.md)，真实无线、手表麦克风和功耗仍待验收。

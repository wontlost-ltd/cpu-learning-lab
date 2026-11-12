# ADR 0010：Apple Watch 前台采集与 iPhone 持久中继

## 状态与范围

沿 P5 原路线扩展 Apple Watch。watchOS 10+ SwiftUI 伴侣 App 复用 PhoneCore/C ABI 的固定音频队列、100 ms 特征和日志；通过 WatchConnectivity live messaging 手动补传给 iPhone，iPhone 再使用已有 TLS 通道向 Mac 提交。

本阶段没有眼镜／Android、后台采集、自动重连无限重试、手机模型教学、跨设备时钟融合或 ChronoSynth 学会事件。模拟器构建与软件协议测试不能代替手表真机、无线链路或功耗验收。

## 身份与密钥边界

Mac 分别为 phone 和 watch 生成独立授权包。iPhone 保存自己的凭据和 watch 专用凭据，使用不同 Keychain account；两者必须属于同一接收端证书，device ID 必须不同。当前只支持一个逻辑 watch 身份，不能静默改写已有日志；同一 watch 的密钥轮换可重新导入。

iPhone 显式启用 WatchConnectivity 后向当前系统配对的 Watch App 提供 `(watch device ID, phone ID)`。Watch 用户核对并确认后，才创建／打开 watch 日志并在 Keychain 保存该身份。配置消息没有服务端证书、TLS 密钥或设备授权 token。Watch 从未收到 Mac 授权密钥。

信任边界是系统配对关系与两个 App 的明确操作，不是硬件证明或额外的消息签名。持有 watch 授权的受托 iPhone 可以提交该逻辑身份的事件；不能声称端到端证明了物理手表传感器。系统切换配对手表后不自动重新激活授权通道，需要用户再次启用／配置。不支持多手表自动授权。

## 采集与生命周期

仅 Watch 用户点击开始后请求麦克风。使用设备实际 8..48 kHz 非交错 Float32 mono/stereo 输入，不请求固定硬件采样率、不在 App 内重采样。继续沿用 8×4096 帧固定 SPSC 队列、后台特征提取与持久提交，回调不提特征、不写盘、不分配或加锁。

离开前台、音频中断或录音中的路由变更时停止，回到前台不自动恢复。只保留特征，不保存原始 WAV，不生成教学标签。watchOS 后台、Always On、锻炼模式、蓝牙耳机和实际格式兼容性均未验收。

沿用 4096 事件／64 来源／16 MiB 等上限，满后拒绝确认、不自动删除。强制同步可能很贵，不声称低功耗；实际手表上的 F_FULLFSYNC、权限、负载、队列溢出与电池成本需要测量。失败时保留已确认记录，关闭重开日志／App 后恢复，不自动清空。

## 两级持久化确认

| 确认 | 实际含义 | 不代表 |
| --- | --- | --- |
| Watch → iPhone ACK | iPhone 的中继日志已提交该批事件 | Mac 收到、模型已学习、事件可删除 |
| iPhone → Mac ACK | 已鉴权的 Mac 接收端提交该批事件 | ChronoSynth 技能通过执行评估 |

Watch 原件、iPhone 中继和 Mac 日志都保留，不在收到回执后删除。Watch 显示“iPhone 曾确认”的历史计数；恢复旧接收快照后，历史回执不自动证明现在仍持有数据。每次补传重新发送当前完整有界快照，接收端按稳定 ID 去重，借此补回旧快照中缺少的事件。

中继日志是受托 watch 源镜像，node 仍为 watch ID，事件 origin 不改写成 phone。iPhone 用专用 watch 凭据向 Mac 发送，从而保留现有来源限制；不能用 phone 密钥冒用 watch。文件同步的不同节点要求不变，中继通过独立 receive API 校验源身份后接收。

## 有界协议与错误语义

配置、批次、回执使用版本化 plist 字典。批次仅允许 6 个规定字段：类型、watch、phone、count、payload、SHA256。每批最多 32 条、payload 最多 32768 字节；拒绝错误身份、摘要、类型、数量和多余字段。布尔 NSNumber 不能当计数。

原生 C API 在持久化前再验证实际事件数量、全部来源和事件格式，整批冲突不部分提交。iPhone 落盘后才回 ACK，Watch 验证同一身份、数量、摘要与插入数，再持久化本地回执。如果 iPhone 提交后回执丢失，Watch 重试不会重复插入。

live messaging 要求会话激活、counterpart reachable。应用额外要求 iPhone 前台且没有中继写入／转发冲突；后台唤醒不是接受经验的许可，忙或不可用时拒绝、不 ACK。采集和本机补传互斥，iPhone 中继接收和 Mac 转发也互斥。

每次 RPC 等待最多 10 秒，取消通过一次性 continuation gate 处理重复回调，不能保证撤回已经交付的系统消息。Watch 每次操作最多 128 批，在发起新批次前检查 60 秒预算；单次 RPC 和磁盘同步可能使墙钟越界，没有硬实时保证。超时／取消不删除原件，只能在双方重新可用后手动重试。

没有 transferUserInfo／transferFile 后台队列、后台录音或自动推送重试，也不把模拟器支持等同于真实无线行为。

## 验证

- C++ 桥接：分页上限、实际数量匹配、重复 receive 零新增、错误源拒绝、回执计数，保留原采集与日志测试。
- Swift：配置包不含 token/pin、严格 envelope、摘要／布尔计数／尺寸／身份拒绝、提交后回执丢失、错误 ACK、不重复增长、载荷数量与源冒用、回执跨重启。
- 注入传输的端到端：watch journal → iPhone relay journal → 真实 Apple TLS → OpenSSL Mac journal；41 条跨批次、重复补传和错误 phone 凭据拒绝。注入段不是真实 WatchConnectivity 无线测试。
- Xcode：iOS 与 watchOS Simulator 编译，保留 iPhone 未配对不能采集／同步的 UI 测试。
- 待真机：Apple 系统配对、配置确认、实际 sendMessage、前后台／锁屏、中断／路由、手表麦克风、断连、功耗与长时间容量行为。

## API 来源

- [Apple WatchConnectivity reachable](https://developer.apple.com/documentation/watchconnectivity/wcsession/isreachable)
- [Apple WatchConnectivity sendMessage](https://developer.apple.com/documentation/watchconnectivity/wcsession/sendmessage(_:replyhandler:errorhandler:))
- [Apple 单 target Watch App](https://developer.apple.com/documentation/watchkit/wkapplication)
- [Apple AVAudioSession](https://developer.apple.com/documentation/avfaudio/avaudiosession)

# ADR 0009：iOS 原生采集与 Apple TLS 客户端

## 选择与范围

沿 P5 原路线先支持用户现有 iPhone/Mac 组合。SwiftUI 负责界面，AVAudioEngine 负责系统麦克风，C ABI 复用 C++20 归纳项目的音频特征、持久事件和版本化批次。Apple Network/Security/CryptoKit 实现 TLS 客户端，iOS 不依赖 OpenSSL。默认桌面应用保持原依赖边界。

交付包括源码、Swift Package、XcodeGen 工程描述、测试、构建与配对操作。没有真机签名安装、App Store 发布、后台采集、视频、Android／眼镜／手表或 ChronoSynth 适配。

## 音频线程与资源边界

仅在明确点击开始且授权成功后配置 AVAudioSession 与输入 tap。系统首选采样率 16 kHz，实际支持 8..48 kHz 非交错 Float32 单／双声道；拒绝不支持的格式，双声道平均为 mono，限幅转 int16，不重采样。

固定 8 槽、每槽 4096 帧的 C++ SPSC 环形队列。64 位原子需编译期保证 lock-free，回调不分配、不上锁、不提特征、不写盘；只有一个生产者和一个消费者。过大、无效输入或满队列整块放弃，计数并推进输入游标；下一有效块通过游标差识别断点。

后台串行 worker 约每 20 ms 尝试最多处理 8 个块，按原生 PcmWindowStream 的 100 ms 窗口提取并强制提交。磁盘慢仍会导致队列溢出，调度间隔不是处理延迟保证。每条成功经验带新会话 ID、序号、片段和帧位置；时间戳是记录时刻，不是硬件采集时钟。

进入后台、音频中断或录音中的路由变更会停止 engine／tap，再排空可处理队列并丢弃尾窗口。系统挂起可能打断清理，重启依赖原生日志恢复；没有后台执行或自动恢复承诺。原始 PCM 只在有界内存中，不保存／传输原始 WAV。

## 持久经验与生命周期

PhoneStore 串行拥有 DeviceJournal；采集与同步互斥。计数只在 C++ receive 成功返回后增加。错误停止处理，不自动清空日志。写入失败后的不确定句柄需要关闭重开，App 可重新启动恢复。日志位于 Application Support，使用 iOS Complete 文件保护；锁屏可能使未完成写入失败，这不被当作确认。

沿用上限：4096 条事件、64 来源、8 回执目标、16 MiB；达到后停止，不驱逐。文件身份固定为配对设备；重导同设备凭据可轮换，不允许静默把旧日志改为另一身份。每次采集新 session 防止重启后的序号碰撞。

当前采集事件没有用户标签，不把自身推断当真值，也不自动应用模型训练。网络复制和回执不是学会事件。手机上的分类模型更新、用户教学 UI、经验应用事务属于后续工作。

## 配对与数据保护

从系统文件选择器读取最多 1025 字节以检测超限，严格解析 CPUAI_PAIR1（实际最大 1024）。用户核对接收节点、设备、完整指纹后确认，配对凭据保存为 WhenUnlockedThisDeviceOnly Keychain 项。App 不打印密钥。首次未配对时采集与同步按钮禁用。

这是已有离线授权方案，不增加匿名注册、硬件证明或云账户。配对包持有者仍可复制身份；文件和设备权限是边界。PrivacyInfo 声明 C++ fstat 用于 App 容器内日志大小／元数据的必要理由，不添加跟踪、遥测或开发者服务。发布前仍需按最终行为审查隐私清单和商店申报。

## TLS 互通与确认

使用 Network.framework TLS 1.3，先验证可信离线包中的完整证书 SHA256 指纹，再以该证书为本连接显式信任锚执行基础 X.509 校验。验证不使用 DNS 主机名，因为身份绑定来自指纹；不做首次连接自动信任，也不安装全系统 CA。验证在独立后台队列执行，不占主线程。

当前新生成接收端身份改为 P-256/SHA256 以支持 Apple TLS；旧 Ed25519 身份不静默改写，使用新目录初始化并重新配对，保留接收日志。握手通过后才发送设备密钥。批次由同一 C++ encodeBatch 生成，长度／数量和 ACK 摘要规则与桌面一致。验证 ACK 后持久化回执，再处理下一批；END/DONE 才报告本次完整完成。每次重传本设备全部有界事件，接收端去重，从而支持 ACK 丢失与旧接收快照恢复。

接收长度先校验再分配，ACK 最多 2048 字节；连接总计时器 10 秒取消 NWConnection，任务取消也取消连接。等待连接／验证失败明确报错，不永久等待；系统磁盘同步仍不受这个网络计时器强制中断。前台手动同步，不提供后台排程、移动网络切换质量或硬实时保证。

## 验证分层

- Linux／macOS／Windows：同一 C ABI 队列、溢出断点、尾部丢弃、特征持久化、批次与回执测试；不需要 Swift 或 Apple SDK。
- macOS：Swift 包编译与解析／重启单元测试；Apple Network.framework 客户端与已有 OpenSSL 服务端的真实 TCP/TLS 集成（证书绑定、错误凭据、断线后重试和回执）。
- iOS Simulator：应用构建和未配对初始状态 UI 测试，确保默认不能采集／同步。
- 待真机：麦克风授权、路由变化、中断、前后台／锁屏、噪声质量、iPhone→Mac 局域网权限、实际延迟／内存／功耗。

模拟器和 Mac TLS 互通不构成真机音频、续航或设备部署验收。

## API 来源

- [Apple AVAudioApplication](https://developer.apple.com/documentation/avfaudio/avaudioapplication)
- [Apple 本地网络 TLS 身份](https://developer.apple.com/documentation/network/creating-an-identity-for-local-network-tls)
- [Apple TLS 版本控制](https://developer.apple.com/documentation/security/sec_protocol_options_set_min_tls_protocol_version(_:_:))
- [Apple 必要理由 API](https://developer.apple.com/documentation/bundleresources/app-privacy-configuration/nsprivacyaccessedapitypes/nsprivacyaccessedapitype)

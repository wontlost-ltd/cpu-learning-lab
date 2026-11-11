# Apple Watch 伴侣采集端

需要 watchOS 10+、iOS 17+、完整 Xcode 26+。默认不录音、不激活 WatchConnectivity。只有手动启用双方连接、手表确认身份并点击开始后才采集。

## 构建

在仓库的 mobile/ios：

```sh
xcodegen generate
xcodebuild -project CPULearningPhone.xcodeproj -scheme CPULearningWatch \
  -destination 'generic/platform=watchOS Simulator' -derivedDataPath ../../build-watch \
  CODE_SIGNING_ALLOWED=NO build
open CPULearningPhone.xcodeproj
```

iPhone target 包含 Watch App。真机需为两个 target 选择自己的开发团队，连接系统已配对的 iPhone／Apple Watch，并按 Xcode 提示安装。模拟器构建不证明麦克风或真实无线链路可用。

## 配置设备

先完成 iPhone 自己的配对。在 Mac 为手表另建凭据，device ID 不能与 iPhone 相同：

```sh
./build-net/cpu-link pair receiver-identity watch-ryan watch-ryan.pair
```

用可信方式把该文件放入 iPhone“文件”，在手机 App 的“Apple Watch 中继”选择“导入手表专用配对包”，核对身份后确认。它必须与 iPhone 的配对包属于同一个接收端证书。Mac 密钥只保存在 iPhone Keychain，不发送给手表。

1. 打开手机和手表 App；双方分别点击“启用手表连接”／“连接 iPhone”。
2. 在手机点击“向手表发送身份配置”。
3. 在手表核对 watch ID 与 phone ID，点击“确认此身份”。
4. 手表点击“开始采集”，需要时允许麦克风；结束后点击停止。
5. 保持双方 App 前台且在范围内，在手表点击“补传到 iPhone”。
6. 手机上输入 Mac 地址与端口，点击“补传手表事件到 Mac”。接收端必须显式绑定局域网地址，具体见 network-sync 示例。

手表收到 iPhone ACK 只表示中继已保存，不表示 Mac 已收到。两端均保留数据，失败后可以重试；重复补传不会重复插入。不要通过删除日志来绕过容量或写入错误。

## 使用边界

只支持一个逻辑 watch 身份，不自动切换或覆盖旧日志；同身份可重新导入轮换密钥。系统切换配对手表后需要手动启用和配置。模型教学、原始 WAV、后台采集、自动续录、眼镜及 ChronoSynth 学会事件均未实现。

每批最多 32 事件／32 KiB，本机日志最多 4096 事件。每条采样窗口 100 ms，达到容量会停止确认，不自动清理。输入格式超限或队列满会丢弃未确认 PCM 并计数；磁盘同步与 radio 成本尚未实测，不声称全天运行或低功耗。

真实 Apple Watch 的权限、格式、无线连接、锁屏／前后台和功耗仍待实测。CI 的传输注入测试只验证契约与持久化；实际 WatchConnectivity 要在系统配对设备上验证。

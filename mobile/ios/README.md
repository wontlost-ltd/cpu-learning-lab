# CPU Learning Phone（iOS 原生采集端）

iOS 17+、SwiftUI、AVAudioEngine、Network.framework、Security/Keychain、CryptoKit；特征、日志和批次序列化复用仓库 C++20 核心。没有 OpenSSL iOS 依赖、LLM、云端服务或预训练模型。

当前是**前台音频特征采集与显式补传工具**，不是手机端通用专家、语音识别器或已经验收的学习应用。不保存原始 WAV，不把预测生成标签，也不产生 ChronoSynth 学会事件。

## 在 Mac 构建与运行

需要完整 Xcode（选中对应 Command Line Tools）、C++20 工具链、Homebrew 的 xcodegen。应用工程从仓库中的 project.yml 可复现生成，不提交个人签名设置。

```sh
brew install xcodegen
cd mobile/ios
swift test
xcodegen generate
open CPULearningPhone.xcodeproj
```

Xcode 选择 CPULearningPhone scheme。模拟器可以检查界面与流程；真机需要在 Signing & Capabilities 中选自己的 Development Team，并连接 iPhone。不要把团队 ID、私钥或配对包提交到仓库。

仅做模拟器构建：

```sh
xcodebuild -project CPULearningPhone.xcodeproj -scheme CPULearningPhone \
  -destination 'generic/platform=iOS Simulator' -derivedDataPath ../../build-ios \
  CODE_SIGNING_ALLOWED=NO build
```

`mobile/ios` 依赖上层仓库头文件，不能只复制这个目录后独立编译。

## 与 Mac 接收端配对

在仓库根目录构建已有接收端：

```sh
brew install cmake openssl@3
cmake -S . -B build-net -DCMAKE_BUILD_TYPE=Release -DCPUAI_WITH_NETWORK=ON -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build-net --parallel
./build-net/cpu-sync init receiver.journal desktop
./build-net/cpu-link init receiver-identity desktop
./build-net/cpu-link pair receiver-identity iphone-ryan iphone-ryan.pair
```

iOS 客户端需要当前版本生成的 P-256 接收端身份；若此前创建的是 Ed25519 身份，请保留原日志，使用新的身份目录重新 init/pair，切勿覆盖原私钥。

初始化路径必须不存在；已有接收日志与身份可继续使用，轮换同设备凭据时使用新的配对文件路径。把配对包通过你信任的私密方式放入 iPhone 的“文件”，在 App 选择“导入配对文件”，核对接收端／设备／证书指纹后确认信任。凭据存入仅本设备、解锁时可访问的 Keychain，不显示密钥。

同一 App 的既有日志绑定设备 ID；可重新导入同设备的新密钥，不支持静默切换成另一个设备身份，也不自动删除旧经验。没有 App 内的清空日志操作。App 卸载不属于可靠持久化保证范围。

## 采集与补传

1. 点击“开始采集”时才请求麦克风权限。保持 App 前台，观察持久化事件数和队列溢出帧。
2. 点击停止，或进入后台／发生音频中断／路由变更时停止。回到前台不会自动恢复录音。
3. Mac 在自己的局域网 IPv4 上启动接收端：

```sh
./build-net/cpu-link serve receiver.journal receiver-identity 7443 100 --bind 192.168.1.10
```

把示例 IP 替换为 Mac 实际地址；iPhone 和 Mac 需网络可达。iPhone 中输入 Mac 地址、端口并点击“同步到 Mac”，首次可能请求本地网络权限。127.0.0.1 在 iPhone 上表示 iPhone 自己，不能用来连接 Mac。

同步只在采集结束后手动启动，进入后台会请求取消。连接失败保留本地事件，重试可去重。接收端日志由服务进程独占，先停止服务，再用 cpu-sync stats 查看。

## 边界与实测

输入为设备实际提供的 8..48 kHz、非交错 Float32 单／双声道，双声道求均值转 PCM16。16000 Hz 是系统首选值提示，不声称设备一定采用；不在 App 中重采样。超出格式明确失败，不静默降级。

回调只写 8×4096 帧的固定 SPSC 队列；特征提取和系统持久化在后台串行队列执行。队列满、过大／无效输入整块丢弃并计数；后续有效输入开启新片段，不拼接断点两侧音频。窗口固定 100 ms，完整提交后才增加持久计数，停止时不完整尾窗口丢弃并计数。

沿用 4096 事件／64 会话来源／16 MiB 等日志上限，满后停止确认，不能无限采集。每窗口强制写入有 I/O 和功耗成本。保存错误时停止采集，关闭并重新打开 App 后重试，不应删除日志来绕过错误。被锁屏、系统挂起、进程终止时只保证软件可恢复范围内的已确认记录，不承诺未确认音频或后台运行。

CI 的 C++ 队列／日志、Swift 单元、Mac 上真实 Apple TLS ↔ OpenSSL 互通和模拟器构建／启动检查不能替代真实 iPhone 麦克风效果、噪声泛化、权限体验、断电和功耗测试。这些结果仍待本机实测。

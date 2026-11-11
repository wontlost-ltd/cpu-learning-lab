# P5：离线日志与幂等补传

构建 `make all test`，或使用已有 CMake 流程。以下演示是同机的两个文件节点，不建立网络服务。

## 采集期间持久化

Mac 已启用可选麦克风构建时：

```sh
./build-mac/cpu-listen --microphone 16000 100 - 100 --journal watch.journal watch
```

其他构建可用原始 PCM16LE 文件：

```sh
./build/cpu-listen --stdin 16000 100 - 100 --journal watch.journal watch < mono-16k.pcm
```

每个 prediction 的 durable_event 表示特征事件已完成持久化。它不包含原始 WAV，不把预测作为用户真值。日志独占期间其他同步进程会报锁错误；先退出监听再同步。每次进程启动使用新会话。

## 断开后重试

```sh
./build/cpu-sync init phone.journal phone
./build/cpu-sync sync watch.journal phone.journal 64 --stop-before-ack
./build/cpu-sync stats phone.journal
./build/cpu-sync sync watch.journal phone.journal 64
./build/cpu-sync sync watch.journal phone.journal 64
```

第一条 sync 故意停在接收端提交之后、发送端记录回执之前，返回码 3；下一次会报告 duplicates 并补记回执。再次同步没有新事件时 delivered=0、acknowledged=0、complete=1。超过 64 条待交付时继续重复，直到 complete=1。

Mac 构建时把上述 `./build/` 替换为 `./build-mac/`。

## 显式用户经验

```sh
./build/cpu-sync init desktop.journal desktop
./build/cpu-sync record desktop.journal recording-one bell recordings/bell.wav
./build/cpu-sync record desktop.journal recording-one - recordings/other.wav
./build/cpu-sync sync desktop.journal phone.journal
```

label 参数 `-` 表示未标注观察；其他标签由用户提供。事件只保存与复制，不会自动修改分类模型；当前仍用 `cpu-learn` 的 audio-teach/audio-exam 完成学习和独立验证。日志不是加密或认证文件。

节点／会话 ID 仅允许 1..64 字节字母、数字、`-_.`。日志达到上限会停止确认新写入，没有自动删除旧经验。完整损坏会明确报错；不要直接删除日志来绕过错误。持久化与资源边界见 ADR 0007。

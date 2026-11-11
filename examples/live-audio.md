# 连续音频：采集、教学、在线纠错

## 无音频设备也可测试

```sh
make all test
./build/cpu-listen --help
./build/cpu-listen --capabilities
./build/cpu-listen --stdin 16000 100 sounds.state 1000 < mono-16k.pcm
```

输入文件必须是无头的、单声道 PCM16LE，不能直接使用 WAV。`sounds.state` 由 P4 `audio-save` 生成，或用 `-` 从未教学状态启动。程序输出逐窗口 JSON 和最终汇总。固定采样率、连续帧即可跨任意管道字节分块；不足一个完整窗口的 EOF 尾部只计数，不补零或教学。

## 可选原生麦克风

准备 C++20、CMake 和已安装的 PortAudio 开发头文件／库。Linux CI 使用 `portaudio19-dev`；其他系统需提供对应安装路径，必要时指定 `PORTAUDIO_INCLUDE_DIR` 与 `PORTAUDIO_LIBRARY`。当前只在 Linux CI 验证链接，没有声称已验证每种桌面设备。

```sh
cmake -S . -B build-mic -DCMAKE_BUILD_TYPE=Release -DCPUAI_WITH_PORTAUDIO=ON
cmake --build build-mic --config Release
./build-mic/cpu-listen --microphone 16000 100 - 600 --capture captured-session 20
```

允许系统麦克风权限并选择系统默认输入设备。命令最多输出 600 个窗口，按 100 ms 窗口约一分钟；驱动 overflow 或等待会延长墙钟时间。只保存前 20 个窗口；目录必须不存在。Ctrl+C 请求结束。未授予权限、无默认设备或采样率不支持会报错，不自动改用其他输入。

真实声音不一定能被八个固定频率探针区分。先使用几种简单提示音，采集后试听、明确标注，再测试独立录音；不根据模型自己的输出给它“自证正确”。

## 用户反馈在窗口边界生效

先录制用户确认的声音并教学：

```text
audio-teach tone1 "提示音A" "captured-session/window-1.wav"
audio-save sounds.state
quit
```

启动监听：

```sh
./build-mic/cpu-listen --microphone 16000 100 sounds.state 600 --reload
```

另开 `cpu-learn`，加载原模型、纠正同一 lesson ID 后保存：

```text
audio-load sounds.state
audio-teach tone1 "修正标签" "captured-session/window-1.wav"
audio-save sounds.state
quit
```

下一完整窗口重新加载成功后，输出 revision 随新模型改变。写入尚未完成时，监听器保留旧模型并增加 reload_failures。需要更可靠的替换时，先保存为同目录临时路径，再用本机文件系统的原子替换完成更新。

默认不保存声音；只有显式 `--capture` 才落盘，目录上限到达后不继续录制。输出 JSON 的 label 为 null 表示未知，字符串 `"unknown"` 可以是用户自己教的普通标签。设备溢出会输出 discontinuity，后续片段重新从 start_frame=0 开始；segment 与 sequence 只在当前进程内有效。

窗口生成会带来至少一个窗口长度的等待；processing_ms 不能代替真实采集延迟或功耗。具体计量范围与待实测项目见 ADR 0006。

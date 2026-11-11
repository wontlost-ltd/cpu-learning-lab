# 本地声音教学

准备单声道 PCM16、8..48 kHz、50 ms..2 s 的 WAV。在项目根目录启动 `./build/cpu-learn`，路径可用双引号括起。

```text
audio-teach door1 "门铃" "recordings/door-train.wav"
audio-teach alarm1 "报警声" "recordings/alarm-train.wav"
audio-ask "recordings/door-new.wav"
audio-teach door1 "新的门铃标签" "recordings/door-train.wav"
audio-save sounds.state
audio-load sounds.state
audio-exam "recordings/holdout.exam"
observe watch 1 1000 -10 4 "recordings/door-new.wav"
observe phone 1 970 20 7 "recordings/alarm-new.wav"
align watch phone 10
align watch phone 11
stream-stats
stream-pop
quit
```

这些标签仅作用户输入示例，程序没有内建门铃识别能力。先用简单、不同频率的提示音验证流程，再测试真实录音。新录音可能返回未知，也可能误识别；没有语音转文字。

`holdout.exam` 格式（至少三条，使用从未教学的独立录音）：

```text
CPUAI_AUDIO_EXAM1
"新的门铃标签" "recordings/door-test1.wav"
"报警声" "recordings/alarm-test1.wav"
"新的门铃标签" "recordings/door-test2.wav"
```

示例中两个事件校正后均为 990 ms；误差和为 11 ms，所以 10 ms 容差没有配对，11 ms 有配对。偏移和误差由调用者提供，不是系统测出来的。

无需录音设备也能执行完整可复现测试：

```sh
make test
python3 tests/integration/audio_cli_test.py build/cpu-learn
```

测试在临时目录生成音调，运行原生解码、特征提取、教学、持久化与独立考试，不留下音频文件。

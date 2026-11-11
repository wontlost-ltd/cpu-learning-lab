# P5：配对后的 TLS 补传

## macOS 构建

需要 C++20 工具链、CMake、OpenSSL 3；测试另需 Python 3。已有 Homebrew 时：

```sh
brew install cmake openssl@3
cmake -S . -B build-net -DCMAKE_BUILD_TYPE=Release -DCPUAI_WITH_NETWORK=ON -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)"
cmake --build build-net --parallel
ctest --test-dir build-net --output-on-failure
```

Linux 可安装对应 OpenSSL 开发包，省略 OPENSSL_ROOT_DIR；仓库 Makefile 也提供 `make test-network`。默认构建没有这个依赖，Windows 网络适配尚未实现。

## 两个节点的本机演示

在接收端初始化日志、身份，并明确授权发送设备：

```sh
./build-net/cpu-sync init receiver.journal desktop
./build-net/cpu-link init receiver-identity desktop
./build-net/cpu-link pair receiver-identity watch watch.pair
```

identity 目录与 pair 文件必须不存在；勿提交、公开或把密钥内容贴入日志。pair 包含服务端证书指纹和设备密钥，跨机器时用私密渠道转交，保持文件权限 0600。同设备再次配对使用新文件，会撤销旧密钥。

另一个终端启动服务：

```sh
./build-net/cpu-link serve receiver.journal receiver-identity 7443 10
```

默认只监听 127.0.0.1；这条命令最多接受 10 条连接，未到数量会继续等待，Ctrl+C 可结束。服务持有接收日志，停止后才能使用 cpu-sync stats。

发送端准备自己的已有经验：

```sh
./build-net/cpu-sync init sender.journal watch
./build-net/cpu-sync record sender.journal recording-one bell recordings/bell.wav
./build-net/cpu-link push sender.journal watch.pair 127.0.0.1 7443
./build-net/cpu-link push sender.journal watch.pair 127.0.0.1 7443
```

第二次发送会报告 duplicates，不重复插入。它只推送 sender 日志中属于 watch 的事件；不会转发其他设备身份。用户标签只是经验载荷，不自动教学。

## 跨电脑与撤销

接收端显式绑定自己的局域网 IPv4 地址：

```sh
./build-net/cpu-link serve receiver.journal receiver-identity 7443 100 --bind 192.168.1.10
```

发送端把 push 的 127.0.0.1 改为该地址；是否能连通取决于本机权限、防火墙与网络。不要把这个参考服务直接公开到互联网。

```sh
./build-net/cpu-link revoke receiver-identity watch
```

成功撤销后旧包不能再提交新批次；若授权文件正被占用，命令会报错，需要重试。重新授权：

```sh
./build-net/cpu-link pair receiver-identity watch watch-new.pair
```

新包替换旧设备密钥。没有硬件绑定或云端账户恢复。

## 断线测试

用 `--drop-after-commit` 启动仅接一条连接的服务，随后 push：

```sh
./build-net/cpu-link serve receiver.journal receiver-identity 7443 1 --drop-after-commit
```

push 会失败，但接收端可能已提交。重新启动普通服务再 push，重复事件去重并补记回执。更多故障测试由 network-cli-tests 自动执行，计量与安全边界见 ADR 0008。

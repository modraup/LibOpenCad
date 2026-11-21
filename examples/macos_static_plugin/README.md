# macOS 静态库插件示例

`dwg_host` 用 `dlopen` 加载 `dwg_plugin.bundle`，再用 `dlsym` 调用 C 入口 `cad_roundtrip`。LibOpenCad 静态链接进插件，宿主只传入输出路径并接收退出状态。

## 编译与运行

以下命令在存放 SDK 压缩包的目录执行。这个预编译包适用于 Apple Silicon 和 macOS 26.0 及以上，构建需要 Xcode Command Line Tools 和 CMake 3.16 及以上。

```sh
unzip -q LibOpenCad-0.1.0-macOS-arm64-static.zip
sdk="$PWD/LibOpenCad-0.1.0-macOS-arm64-static"
cmake -S "$sdk/share/doc/LibOpenCad/examples/macos_static_plugin" -B plugin-build \
  -DCMAKE_PREFIX_PATH="$sdk" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_DEPLOYMENT_TARGET=26.0
cmake --build plugin-build --parallel
./plugin-build/dwg_host ./plugin-build/dwg_plugin.bundle ./plugin-build/sample.dwg
```

插件创建一个 R2018（AC1032）DWG，写入从 `(1, 2, 3)` 到 `(4, 6, 8)` 的 LINE。输出路径已有文件时会被覆盖。插件关闭文件，再重新打开并核对版本、LINE 数量和两个端点。验证成功时宿主打印 `Verified AC1032 DWG and LINE endpoints`，退出码为 0。

在仓库中开发时，也可将 `-S` 指向 `examples/macos_static_plugin`，并沿用上述静态 SDK 路径。运行 `(cd plugin-build && ctest --output-on-failure)` 可重复验证。用 `otool -L plugin-build/dwg_plugin.bundle` 查看插件实际加载的系统库。

# Compile Prober

Compile Prober 是一个面向 GCC C++ 前端的编译期追踪插件。它把一次编译中的预处理文件、函数解析、作用域和优化 pass 转换为 Chrome Trace JSON，帮助定位编译时间热点。

## 功能

- 记录编译单元总耗时
- 记录每次文件进入和离开产生的预处理区间
- 记录函数解析的开始与结束时间
- 记录命名空间和类作用域事件
- 记录 GCC 优化 pass 的时间区间
- 使用单调时钟，避免系统时间调整影响结果
- 输出 Chrome Trace 完整时长事件：`ph: "X"`、`ts`、`dur`
- 在写出前统一按事件开始时间排序
- 过滤持续时间小于 1 ms 的短事件，减少 trace 噪声

## 支持范围

当前实现依赖 GCC C++ 前端 API，例如 `cp/cp-tree.h` 和 `decl_as_string()`，因此主要面向 G++ 编译的 C++ 源文件。插件需要与加载它的 GCC 版本使用匹配的 GCC plugin headers。

## 环境要求

- GCC 和对应的 GCC plugin development headers
- CMake 3.12 或更高版本
- 支持 C++20 的编译器
- GNU Make 或其他 CMake generator

在 Ubuntu 上可以安装 GCC 11 的开发依赖：

```bash
sudo apt-get update
sudo apt-get install -y build-essential cmake gcc-11-plugin-dev
```

项目也提供了开发镜像配置：[docker/dev.dockerfile](docker/dev.dockerfile)。

## 构建

默认会同时构建插件和测试 fixture：

```bash
cmake -S . -B build
cmake --build build
```

如果只需要构建插件，可以关闭测试：

```bash
cmake -S . -B build -DCOMPILE_PROBER_BUILD_TEST=OFF
cmake --build build
```

构建产物为：

```text
build/compile-prober.so
```

如果 GCC plugin headers 不在当前 C++ 编译器的默认位置，可以显式指定：

```bash
cmake -S . -B build \
  -DCOMPILE_PROBER_GCC_PLUGIN_DIR=/path/to/gcc/plugin
```

## 使用插件

指定 trace 输出文件：

```bash
g++ \
  -fplugin=build/compile-prober.so \
  -fplugin-arg-compile-prober-trace=/tmp/compile-trace.json \
  -c your_file.cpp
```

也可以指定输出目录，让插件自动生成临时文件名：

```bash
g++ \
  -fplugin=build/compile-prober.so \
  -fplugin-arg-compile-prober-trace-dir=/tmp \
  -c your_file.cpp
```

不传输出参数时，插件会在 `/tmp` 下生成 `trace_XXXXXX.json`。

生成的 JSON 可以导入 Chrome Trace 查看器：

- `chrome://tracing`
- `about://tracing`

## 工作流程

```text
GCC plugin_init
        |
        v
注册 GCC 回调和 libcpp file_change hook
        |
        +--> 预处理 enter/leave -> frame 栈 -> 预处理事件
        |
        +--> 函数解析 start/finish -> decl 时间表 -> 函数事件
        |
        +--> pass execution -> 当前 pass -> pass 事件
        |
        v
编译结束：汇总、过滤、排序、输出 Chrome Trace JSON
```

预处理事件按照每次进入创建独立 frame，因此同一个文件被多次处理时不会覆盖之前的记录。函数事件使用同一个 `decl` 在 start 和 finish 回调之间配对。输出阶段使用完整时长事件，不依赖 B/E 事件栈进行配对。

## 测试

构建完成后运行 CTest：

```bash
ctest --test-dir build --output-on-failure
```

测试包含：

- 编译 `test/test.cc` fixture，并加载 Compile Prober 插件
- 检查 trace 文件是否生成
- 检查 JSON 是否包含 `traceEvents`
- 检查事件是否使用 `ph: "X"` 和 `dur`
- 检查旧的 B/E 事件格式不会出现在输出中

## 目录结构

```text
include/              公共类型和模块接口
src/plugin.cc         GCC 插件入口和回调注册
src/tracking.cc       预处理、函数和 pass 事件采集
src/perf_output.cc    事件过滤、排序和 JSON 输出
test/                 CTest fixture 和 trace 格式验证
docker/               GCC plugin 开发环境配置
```

## 已知限制

- 当前依赖 GCC C++ 前端，尚未提供 C 前端适配层。
- 优化 pass 使用 GCC 的 `PLUGIN_PASS_EXECUTION` 回调进行区间近似，区间可能包含 pass 调度间隔。
- 默认会过滤小于 1 ms 的事件，短事件的累计开销可能不会出现在 trace 中。
- 当前输出面向单个 GCC 编译进程；多编译单元的聚合需要由外部工具完成。

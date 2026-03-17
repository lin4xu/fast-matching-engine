# Fast Matching Engine

本项目是一个基于 C++ 打造的极速高频撮合引擎（HFT Matching Engine）。通过 **$3 \times 3$ 终极消融实验 (Ablation Study)** 与 **Linux 微架构级指令剖析 (Micro-architecture Profiling)**，量化剖析了底层数据结构、内存布局、硬件级寻址与 CPU 缓存之间在极端高频环境下的博弈。

## 🔬 $3 \times 3$ 终极消融实验

我们在模拟实盘高频报单/撤单的稳态交织（Interleaved Steady-State）场景下，针对两大正交维度进行了严谨的 9 组变体压测。

* **维度一：盘口寻址算法 (3种)**
  * `Map`: 基于红黑树 (`std::map`)，时间复杂度 $O(\log N)$。
  * `Array`: 价格转数组下标，while 循环线性扫描寻找最优报价，时间复杂度 $O(K)$。
  * `Bitset`: 极致降维，采用硬件前导零/尾随零指令 (`lzcnt` / `tzcnt`) 进行分层位图寻址，时间复杂度逼近物理 $O(1)$。

* **维度二：内存与节点生命周期 (3种)**
  * `SysAlloc + StdLst`: 使用标准库 `std::list`，每次创建订单伴随双重系统内存调用 (`new Order` 及隐式链表节点)，缓存极不友好。
  * `PoolAlloc + StdLst`: 采用预分配的 Union 内存池，消除了 `Order` 本身的分配，但未解决 `std::list` 底层隐式节点的小对象分配问题。
  * `PoolAlloc + Intru`: **侵入式链表 (0分配)**，彻底抛弃 STL，将 `prev` 与 `next` 指针内置于订单结构。运行时达到绝对的 Zero Allocation 与物理内存严格连续。

---

## 🚀 极致性能跑分 (Benchmark Results)

*测试环境：Ubuntu 22.04 (WSL2) / GCC 13 / `-O3 -march=native`，100,000 笔存量盘口深度下的稳态交织事件。*

| 实验组别 (Algorithm_Memory) | 吞吐量 (Ops/sec) | p50 (ns) | p90 (ns) | p99 (ns) | p99.9 (ns) | 性能倍率 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `BM_Map_SysAlloc_StdLst` (基准线) | 15.46 M/s | 48.78 | 96.32 | 180.65 | 281.52 | 1.00x |
| `BM_Map_PoolAlloc_StdLst` | 15.91 M/s | 49.60 | 93.42 | 172.38 | 240.17 | 1.02x |
| `BM_Map_PoolAlloc_Intru` | 17.71 M/s | 41.34 | 76.47 | 126.91 | 192.23 | 1.14x |
| `BM_Arr_SysAlloc_StdLst` | 18.48 M/s | 28.54 | 84.81 | 156.38 | 216.78 | 1.19x |
| `BM_Arr_PoolAlloc_StdLst` | 20.32 M/s | 26.91 | 78.26 | 141.21 | 201.26 | 1.31x |
| `BM_Arr_PoolAlloc_Intru` | 22.85 M/s | 24.39 | 66.14 | 112.03 | 145.51 | 1.47x |
| `BM_Bit_SysAlloc_StdLst` | 18.76 M/s | 31.85 | 79.02 | 141.49 | 196.52 | 1.21x |
| `BM_Bit_PoolAlloc_StdLst` | 20.93 M/s | 26.87 | 64.48 | 121.53 | 280.69 | 1.35x |
| **`BM_Bit_PoolAlloc_Intru` (大满贯)** | **25.38 M/s** | **24.39** | **44.64** | **72.34** | **99.21** | **1.64x** |

---

## 🔬 微观性能瓶颈剖析 (Micro-architecture Profiling)

为了探究 25.38 M/s 吞吐量背后的物理极限，本项目基于 Linux `perf` 工具对大满贯引擎 (`EngineBitsetIntrusive`) 进行了纳秒级的软件采样与汇编级指令剖析，揭示了以下硬核工程真相：

1. **系统内存分配是绝对的“性能毒药”**
   `perf report` 采样显示，非侵入式版本在稳态运行中，调用 `cfree`、`operator new` 和 `malloc` 分别消耗了 **4.94%、4.75% 和 4.29%** 的整体 CPU 算力。这证明了在高频场景中，高达 14% 的性能被白白浪费在了操作系统级别的内存锁和堆分配上，印证了 **Zero-Allocation (内存池+侵入式链表)** 的必要性。

2. **一维位图的线性扫描代价 (Linear Scan Cost)**
   通过 `perf annotate` 分析生成的汇编视图证实，`-march=native` 成功触发了极速硬件指令 `lzcnt`/`tzcnt`。然而，为了遍历 64-bit 的分块数组，外层包裹的 `for` 循环（循环变量递减 `sub`、条件判断 `test` 与跳转 `jmp`）依然吃掉了约 **7%** 的指令周期。这为我们指向了下一代优化的核心：实现纯粹的 $O(1)$ 多级位图。

3. **隐藏的指令刺客：整数除法 (`divl`)**
   在 `price_to_index` 价格映射函数中，虽然加减法仅需 1 个时钟周期，但除以 `tick_size_` 触发了极度昂贵的 x86 `divl` 除法指令。这导致 CPU 管线在此处产生了约 **5.25% 的停顿 (Pipeline Stall)**。

---

## 🗺️ 架构与算法演进路线 (Future Roadmap)

对标华尔街顶尖的极速交易系统，本项目未来的优化方向已被上述剖析明确指明：

### 1. 核心算法与硬件级寻址再进化
* **多级位图寻址 (Multi-level Bitset / 64-ary Tree)：**
  彻底消灭一维 Bitset 的 `for` 循环瓶颈。引入 L1 摘要层进行两级连续的 `lzcnt`，无论盘口价差（Spread）多大，都能保证在严格的 2~3 个时钟周期内锁定档位，达到绝对的 $O(1)$。
* **编译期除法优化 (Template Tick Size)：**
  将 `tick_size` 和 `min_price` 重构为 C++ 模板参数。利用编译器在编译期的“魔法数字乘法优化 (Magic Number Multiplication)”，用极低开销的乘法和位移指令彻底消灭 `divl` 带来的 5% 管线停顿。

### 2. 内存池与底层架构改造
* **分块内存池 (Chunked Arena Allocator)：**
  解决目前固定长度 `PoolAllocator` 在容量耗尽时退化为 `new` 所导致的“性能悬崖”。采用分块增长策略，不仅避免单点毛刺，还能保障新区块的物理连续性。
* **彻底的去虚函数化 (CRTP & Static Dispatch)：**
  消除 `IMatchingEngine` 基类的 `virtual` 关键字与 `std::function` 回调。使用奇异递归模板模式（CRTP）实现编译期多态，消灭最后几十纳秒的 vtable 寻址与分支预测失败（Branch-misses）开销。
* **CPU 缓存行严格对齐 (Cache-line Alignment)：**
  对 `Order` 等核心数据结构引入 `alignas(64)`，保证对象尺寸完美对齐 64-Byte Cache Line，防止多线程/多核场景下的伪共享（False Sharing），并实现极致的硬件预取（Prefetching）。

---

## 🛠️ 编译与运行指南

* **推荐环境**: Linux (Ubuntu 22.04+) / WSL2 
* **编译器**: GCC 11+ / Clang 14+ 
* **构建步骤**:

```bash
# 1. 生成 Release 模式的 CMake 配置
cmake -B build -DCMAKE_BUILD_TYPE=Release

# 2. 开启多核极速编译
cmake --build build -j 8

# 3. 运行一致性校验测试 (Shadow Testing)
./build/matching_tests

# 4. 运行大满贯极致性能跑分
./build/run_benchmarks
```

## 📊 微架构性能分析 (Profiling)

本项目强烈推荐使用 Linux 内核级工具 `perf` 进行非侵入式性能调优：

```bash
# [宏观体检] 查看 IPC (每周期指令数) 与 CPU 管线、缺页异常等总体健康度
perf stat -d ./build/run_benchmarks

# [微观采样] 录制函数调用栈与指令级执行热点
perf record -g ./build/run_benchmarks

# [交互式分析] 查看耗时排行榜 (在 TUI 界面中光标选中函数 -> 按回车 -> 选择 Annotate 查看汇编级耗时)
perf report

# [导出报告] 在无头模式下将完整的调用栈和汇编级分析导出为纯文本 (方便跨设备查看)
perf report --stdio > perf_report.txt
perf annotate --stdio > perf_annotate.txt
```
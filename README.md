# Fast Matching Engine

本项目是一个基于 C++ 打造的极速高频撮合引擎（HFT Matching Engine）。通过 **$3 \times 3$ 终极消融实验 (Ablation Study)**，量化剖析了底层数据结构、内存布局、硬件级寻址与 CPU 缓存之间在极端高频环境下的博弈。

## 🔬 $3 \times 3$ 终极消融实验

我们在模拟实盘高频报单/撤单的稳态交织（Interleaved Steady-State）场景下，针对两大正交维度进行了严谨的 9 组变体压测。

* **维度一：盘口寻址算法 (3种)**
  * `Map`: 基于红黑树 (`std::map`)，时间复杂度 $O(\log N)$。
  * `Array`: 价格转数组下标，while 循环线性扫描寻找最优报价，时间复杂度 $O(K)$。
  * `Bitset`: 极致降维，采用 `_BitScanReverse64` / `__builtin_clzll` 硬件前导零指令进行分层位图寻址，时间复杂度逼近物理 $O(1)$。

* **维度二：内存与节点生命周期 (3种)**
  * `SysAlloc + StdLst`: 使用标准库 `std::list`，每次创建订单伴随双重系统内存调用 (`new Order` 及隐式链表节点)，缓存极不友好。
  * `PoolAlloc + StdLst`: 采用预分配的 Union 内存池，消除了 `Order` 本身的分配，但未解决 `std::list` 底层隐式节点的小对象分配问题。
  * `PoolAlloc + Intru`: **侵入式链表 (0分配)**，彻底抛弃 STL，将 `prev` 与 `next` 指针内置于订单结构。运行时达到绝对的 Zero Allocation 与物理内存严格连续。

---

## 🚀 极致性能跑分 (Benchmark Results)

*注：以下测试采用了真实的硬件时间戳计数器指令 (`__rdtscp`) 进行纳秒级精密测量，彻底消除了常规操作系统 API 的系统调用开销与测量墙。测试环境为 100,000 笔存量盘口深度下的稳态交织事件。*

| 实验组别 (Algorithm_Memory) | 吞吐量 (Ops/sec) | p50 (ns) | p90 (ns) | p99 (ns) | p99.9 (ns) | 性能倍率 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `BM_Map_SysAlloc_StdLst` (基准线) | 9.30 M/s | 82.72 | 210.11 | 388.37 | 1863.72 | 1.00x |
| `BM_Map_PoolAlloc_StdLst` | 11.52 M/s | 70.69 | 165.77 | 274.50 | 719.32 | 1.23x |
| `BM_Map_PoolAlloc_Intru` | 14.93 M/s | 47.95 | 91.76 | 148.39 | 478.26 | 1.60x |
| `BM_Arr_SysAlloc_StdLst` | 13.33 M/s | 55.40 | 138.50 | 227.81 | 299.75 | 1.43x |
| `BM_Arr_PoolAlloc_StdLst` | 16.41 M/s | 39.26 | 108.29 | 230.23 | 596.04 | 1.76x |
| `BM_Arr_PoolAlloc_Intru` | 22.40 M/s | 24.39 | 69.03 | 115.75 | 155.44 | 2.40x |
| `BM_Bit_SysAlloc_StdLst` | 13.78 M/s | 52.91 | 116.58 | 193.47 | 272.85 | 1.48x |
| `BM_Bit_PoolAlloc_StdLst` | 17.48 M/s | 38.04 | 82.29 | 138.53 | 213.38 | 1.87x |
| **`BM_Bit_PoolAlloc_Intru` (大满贯)** | **24.93 M/s** | **26.04** | **46.30** | **74.82** | **105.83** | **2.68x** |

### 💡 核心工程洞察 (Engineering Insights)
1. **碾压级的尾部延迟控制：** 基准线引擎的 `p99.9` 延迟高达 `1863 ns`，存在极大的毛刺；而大满贯组（Bitset + Intrusive）通过消除所有堆分配并利用硬件位图指令，将极限尾部延迟死死压制在 **`105 ns`**，体现了确定性（Determinism）对于 HFT 系统的绝对重要性。
2. **STL 是 HFT 的“毒药”：** 在 Bitset 组中，仅仅将 `std::list` 替换为侵入式链表（Intrusive List），吞吐量就从 `17.48 M/s` 飙升至 `24.93 M/s`。这是因为消除了动态小对象分配带来的指令开销，同时极大提高了 L1/L2 CPU Cache 的命中率。

---

## 🗺️ 架构与算法演进路线 (Future Roadmap)

尽管当前的大满贯引擎已经达到了约 2500万笔/秒 的极高吞吐，但若要对标真实的华尔街顶尖极速交易系统，本项目在未来仍有以下深度优化空间：

### 1. 核心算法与数据结构再进化
* **多级位图寻址 (Multi-level Bitset / 64-ary Tree)：**
  当前一维 Bitset 在遇到极大的盘口价差（Spread）时，硬件指令扫码会退化为 $O(K/64)$ 线性扫描。引入 L1 摘要层进行两级 `__builtin_clzll`，无论价差多大都能保证严格的 2 个时钟周期内锁定档位。
* **高密度哈希映射 (Swiss Table / Robin Hood Hash)：**
  为了兼容离散的 UUID 或哈希化的 `order_id`，避免全局定长 `std::vector` 映射导致 OOM，未来将引入基于开放寻址的极致哈希表（如 `absl::flat_hash_map`）或 Sparse Set。

### 2. 内存池与底层架构改造
* **分块内存池 (Chunked Arena Allocator)：**
  解决目前固定长度 `PoolAllocator` 在容量耗尽时退化为 `new` 所导致的“性能悬崖”。采用分块增长策略，不仅避免单点毛刺，还能保障新区块的物理连续性。
* **彻底的去虚函数化 (CRTP & Static Dispatch)：**
  消除 `IMatchingEngine` 基类的 `virtual` 关键字与 `std::function` 回调。使用奇异递归模板模式（CRTP）实现编译期多态，消灭最后几十纳秒的 vtable 寻址与间接调用开销。
* **CPU 缓存行严格对齐 (Cache-line Alignment)：**
  对 `Order` 等核心数据结构引入 `alignas(64)`，保证对象尺寸完美对齐 64-Byte Cache Line，防止多线程/多核场景下的伪共享（False Sharing），并实现极致的硬件预取（Prefetching）。

---

## 🛠️ 编译与测试环境

* **构建系统**: CMake 3.14+
* **编译器**: MSVC 19.44+ / GCC 11+ / Clang 14+ 
* **编译优化**: 全面启用 `-O3` / `/O2`、LTO/IPO，强制开启 `AVX2`/`AVX-512` 指令集。
* **安全性保障**: 在 Debug 模式下全量接入 Google Test 以及 ASan/UBSan 进行内存与未定义行为捕获。
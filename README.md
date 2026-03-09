# Fast Matching Engine

本项目基于 C++ 打造的高频撮合引擎，采用了量化交易工业界的极致性能优化手段。通过 **$3 \times 3$ 终极消融实验 (Ablation Study)**，量化剖析了底层数据结构、内存布局、硬件级寻址与 CPU 缓存之间的博弈。

## $3 \times 3$ 终极消融实验

我们在模拟实盘高频报单/撤单的场景下，针对两大正交维度进行了严谨的 9 组变体测试。为了保证实验的纯粹性，**所有变体均统一采用 $O(1)$ 的定长数组实现基于 `order_id` 的全局撤单映射**。

* **维度一：盘口寻址算法 (3种)**
  * `Map`: 基于红黑树 (`std::map`)，时间复杂度 $O(\log N)$。
  * `Array`: 价格转数组下标，while 循环线性扫描寻找最优报价，时间复杂度 $O(K)$。
  * `Bitset`: 极致降维，采用 `_BitScanReverse64` / `__builtin_clzll` 硬件前导零指令进行分层位图寻址，时间复杂度真正的物理 $O(1)$。

* **维度二：内存与节点生命周期 (3种)**
  * `SysAlloc + StdLst`: 使用标准库 `std::list`，每次创建订单伴随双重系统内存调用 (`new Order` 及隐式链表节点)。
  * `PoolAlloc + StdLst`: 采用 `Union` 数据与控制合一的预分配内存池，消除了 `Order` 本身的分配，但未解决 `std::list` 底层小对象分配问题。
  * `PoolAlloc + Intru`: **侵入式链表 (0分配)**，彻底抛弃 STL，将 `prev` 与 `next` 指针内置于订单结构。运行时达到绝对的 Zero Allocation 与物理内存连续。

---

### 跑分结果 (稳态交织压测 - 100,000 笔存量盘口深度)

*注：以下测试跑分采用稳态交织模型 (Interleaved Steady-State)，即在维持 10 万盘口深度的前提下，交织进行随机的 Add 和 Cancel 操作，以真实反映 HFT 场景极端的缓存失效率。测试系统为 Windows MSVC Release。*

| 实验组别 (Algorithm_MemoryAllocator) | 吞吐量 (Ops/sec) | p50 (ns) | p90 (ns) | p99 (ns) | p99.9 (ns) | 性能相对提升 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `BM_Map_SysAlloc_StdLst` (基准线) | 9.10 M/s | 100 | 200 | 300 | 600 | 1.00x |
| `BM_Map_PoolAlloc_StdLst` | 11.38 M/s | 100 | 200 | 300 | 400 | 1.25x |
| `BM_Map_PoolAlloc_Intru` | 15.57 M/s | 0* | 100 | 200 | 200 | 1.71x |
| `BM_Arr_SysAlloc_StdLst` | 13.52 M/s | 100 | 100 | 200 | 300 | 1.49x |
| `BM_Arr_PoolAlloc_StdLst` | 16.29 M/s | 0* | 100 | 200 | 300 | 1.79x |
| `BM_Arr_PoolAlloc_Intru` | 20.00 M/s | 0* | 100 | 100 | 200 | 2.20x |
| `BM_Bit_SysAlloc_StdLst` | 14.63 M/s | 100 | 100 | 200 | 300 | 1.61x |
| `BM_Bit_PoolAlloc_StdLst` | 17.48 M/s | 0* | 100 | 200 | 500 | 1.92x |
| **`BM_Bit_PoolAlloc_Intru` (大满贯)** | **20.97 M/s** | **0*** | **100** | **100** | **300** | **2.30x** |

*(注：由于高精度时钟的测量墙限制，执行快于 100ns 时 p50 会被截断为 0 ns，这意味着系统常态运转已经快过了常规 API 的测量极限。)*

---

### 核心工程洞察 (Engineering Insights)

1. **硬件级 O(1) > 软件层 O(1)：**
   `EngineArray` 虽然把寻找档位变成了常数时间映射，但在盘口价格存在价差 (Spread) 时，其依赖的 `while(index >= 0)` 会退化为 $O(K)$ 线性扫描。而 `EngineBitset` 利用 CPU 硬件级指令 (`__builtin_clzll` / `_BitScanReverse64`)，哪怕盘口中间空了 1000 个价差，依然是 **单条机器指令周期** 返回结果，吞吐率逼近 2100 万大关。
2. **STL 是 HFT 系统的“毒药”：**
   即便我们用了最强的 Bitset 算法，如果底层还使用 `std::list` (如 `BM_Bit_PoolAlloc_StdLst`)，其性能依然无法突破 18 M/s 的瓶颈。这是由于 `std::list` 在 `push_back` 时会触发不可控的小对象动态分配。只有引入侵入式链表 (Intrusive List) 做到 **Zero Allocation**，才能释放硬件所有的算力。
3. **Union 内存池的奇效：**
   在 `PoolAllocator` 中，我们采用了 `union` 将空闲时的内存块本身复用为指向下一块的指针 (`next_free`)。这彻底消除了 `std::vector<T*>` 管理空闲块带来的额外内存和 Cache Miss，实现了最高密度的内存布局。

---

### 编译与测试环境
* **编译器**: MSVC 19.44 / GCC 11+ (兼容 `__builtin_clzll`)
* **操作系统**: Windows 11 x64 / Linux
* **构建系统**: CMake 3.14+
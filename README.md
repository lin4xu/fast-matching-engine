# Fast Matching Engine

本项目基于 C++ 打造的高频撮合引擎，采用了量化交易工业界的极致性能优化手段，通过 **$2 \times 2 \times 2$ 严格消融实验 (Ablation Study)**，量化剖析了底层数据结构、内存布局与 CPU 缓存之间的博弈。

## $2 \times 2 \times 2$ 终极消融实验

我们在模拟实盘高频报单/撤单的场景下，针对三大正交维度进行了严谨的消融测试：
* **寻址算法**：`Map` (红黑树) vs `Array` (定长数组)
* **节点结构**：`StdLst` (标准库动态分配) vs `Intru` (侵入式零分配)
* **内存管理**：`SysAlloc` (系统 new) vs `PoolAlloc` (预分配内存池)

### 测试结果 (稳态交织压测 - 100,000 笔存量盘口深度)

*注：以下测试跑分采用稳态交织模型 (Interleaved Steady-State)，即在维持 10 万盘口深度的前提下，交织进行随机的 Add 和 Cancel 操作，以真实反映 HFT 场景。*

| 实验组别 (Algorithm_Container_Allocator) | 吞吐量 (Ops/sec) | p50 (ns) | p90 (ns) | p99 (ns) | p99.9 (ns) | 性能提升 |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| `BM_Map_StdLst_SysAlloc` (基准线) | 7.95 M/s | 100 | 300 | 400 | 600 | 1.00x |
| `BM_Map_StdLst_PoolAlloc` | 8.11 M/s | 100 | 200 | 400 | 500 | 1.02x |
| `BM_Arr_StdLst_SysAlloc` | 9.18 M/s | 100 | 200 | 300 | 500 | 1.15x |
| `BM_Arr_StdLst_PoolAlloc` | 11.85 M/s | 100 | 200 | 300 | 400 | 1.49x |
| `BM_Map_Intru_SysAlloc` | 12.25 M/s | 100 | 100 | 200 | 300 | 1.54x |
| `BM_Map_Intru_PoolAlloc` | 14.88 M/s | 0* | 100 | 200 | 200 | 1.87x |
| `BM_Arr_Intru_SysAlloc` | 15.25 M/s | 0* | 100 | 200 | 300 | 1.91x |
| **`BM_Arr_Intru_PoolAlloc` (大满贯形态)** | **20.00 M/s** | **0*** | **100** | **100** | **200** | **2.51x** |

*(注：由于 Windows 高精度时钟精度墙限制，单次操作快于 100ns 时，p50 会被截断显示为 0 ns。这证明了引擎的实际速度已经击穿了常规系统时钟的测量下限。)*

### 核心工程洞察 (Engineering Insights)

1. **木桶效应与算法降维：** 当系统受限于频繁的堆内存分配时，即便将 $O(\log N)$ 的红黑树升级为 $O(1)$ 的数组（`Map_StdLst_SysAlloc` -> `Arr_StdLst_SysAlloc`），提升也仅有 15%。算法并非唯一的性能银弹。
2. **STL 是高频交易的毒药：** 从 `BM_Arr_StdLst_PoolAlloc` 到 `BM_Arr_Intru_PoolAlloc`，仅仅是剥离了 `std::list` 底层的 `Node` 内存分配，吞吐量直接**暴涨接近一倍**，尾部延迟（p99.9）从 400ns 收敛到了惊人的 200ns。
3. **缓存预测与物理连续：** 单独使用数组或内存池无法达成极限。只有将 **数组 + 侵入式链表 + 内存池** 三者强强联手，让数据在物理内存上达到绝对的连续排布，才能跑出 **2000万次/秒** 的物理极限！

---

## Next Steps: 走向生产级 HFT

虽然目前的消融实验已经榨干了常规优化的性能，但距离**极限生产要求**仍有进一步演进空间，这也是本项目未来的优化方向：

### 1. 彻底的零分配：侵入式 Free-List (Intrusive Memory Pool)
目前的 `PoolAllocator` 虽然预分配了内存，但依然维护了一个 `std::vector<T*> free_list_` 来管理空闲块。在极端的缓存敏感场景下，操作这个额外的动态数组会引发额外的 Cache Miss。
**改进方案：** 利用 `union` 技巧，将空闲时的 `Order` 内存块本身用作指向下一个空闲块的指针 (`next_free`)。彻底消除额外的 `free_list_` 数组，实现真正意义上的数据与控制结构内存合一。

### 2. O(1) 盘口极值搜索：分层位图 (Hierarchical Bitset)
在 `EngineArray` 中，当某个价格档位的订单被消耗空时，系统采用 `while` 循环线性扫描寻找下一个有效价格档位。如果遇到深度较差、价差 (Spread) 很大的盘口，这里的扫描开销会引发不确定的延迟毛刺。
**改进方案：** 引入位图数据结构 (Bitset)。通过底层硬件指令 `__builtin_clz` (Count Leading Zeros) 或 `_BitScanReverse`，能够在单条时钟周期内， $O(1)$ 定位到下一个非空的档位索引，彻底消除线性扫描带来的尾部延迟 (Tail Latency)。

### 测试环境说明
* **编译器**: MSVC 19.44
* **操作系统**: Windows 11 x64
# Fast Matching Engine

本项目基于 C++ 打造的高频撮合引擎，采用了量化交易工业界的极致性能优化手段，通过 **$2 \times 2 \times 2$ 严格消融实验 (Ablation Study)**，量化剖析了底层数据结构、内存布局与 CPU 缓存之间的博弈。

## $2 \times 2 \times 2$ 终极消融实验

我在模拟实盘高频报单/撤单的场景下，针对三大正交维度进行了严谨的消融测试：
* **寻址算法**：`Map` (红黑树) vs `Array` (定长数组)
* **节点结构**：`StdLst` (标准库动态分配) vs `Intru` (侵入式零分配)
* **内存管理**：`SysAlloc` (系统 new) vs `PoolAlloc` (预分配内存池)



### 测试结果 (100,000 笔密集订单实盘模拟)

| 实验组别 (Algorithm_Container_Allocator) | 耗时 (ns) | 吞吐量 (Ops/sec) | 性能提升 |
| :--- | :--- | :--- | :--- |
| `BM_Map_StdLst_SysAlloc` (基准线) | 17,314,189 | 11.55 M/s | 1.0x |
| `BM_Map_StdLst_PoolAlloc` | 15,625,000 | 12.80 M/s | 1.1x |
| `BM_Arr_StdLst_SysAlloc` | 14,375,000 | 13.91 M/s | 1.2x |
| `BM_Arr_StdLst_PoolAlloc` | 11,474,609 | 17.42 M/s | 1.5x |
| `BM_Map_Intru_SysAlloc` | 9,166,667 | 21.81 M/s | 1.8x |
| `BM_Map_Intru_PoolAlloc` | 6,696,429 | 29.86 M/s | 2.5x |
| `BM_Arr_Intru_SysAlloc` | 6,406,250 | 31.21 M/s | 2.7x |
| **`BM_Arr_Intru_PoolAlloc` (终极形态)** | **3,766,741** | **53.09 M/s** | **4.6x** |

### 核心工程洞察 (Engineering Insights)

1. **木桶效应与算法降维：** 当系统受限于频繁的堆内存分配时，即便将 $O(\log N)$ 的红黑树升级为 $O(1)$ 的数组，提升也仅有 20%。算法并非唯一的性能银弹。
2. **STL 是高频交易的毒药：** 从 `BM_Arr_StdLst_PoolAlloc` 到 `BM_Arr_Intru_PoolAlloc`，仅仅是剥离了 `std::list` 底层的 `Node` 内存分配，吞吐量直接**爆涨 3 倍**。
3. **缓存撕裂与大满贯：** 单独使用数组或内存池无法达成极限。只有将 **数组 + 侵入式链表 + 内存池** 三者强强联手，让数据在物理内存上达到绝对的连续排布，配合 CPU 缓存预测，才能跑出 **5300万次/秒**（单次报/撤单仅耗时 ~18.8 ns）的物理极限！
# 北京邮电大学计算机网络实验报告

## 封面信息

| 项目 | 内容 |
| --- | --- |
| 实验名称 | 计算机网络实验一：数据链路层滑动窗口协议的设计与实现 |
| 学院 | `待填写` |
| 专业班级 | `待填写` |
| 学号 | `待填写` |
| 姓名 | `待填写` |
| 指导教师 | `待填写` |
| 完成日期 | `待填写` |

> 导出 PDF 前，建议按课程给出的正式封面样式重新排版首页，并补全“成绩评定 / 评语 / 教师签名”区域。

## 摘要

本实验基于课程提供的数据链路层仿真平台，分别实现了 Go-Back-N 与 Selective Repeat 两种滑动窗口协议，用于在带宽为 `8000 bps`、单向传播时延为 `270 ms`、默认误码率为 `10^-5` 的全双工卫星信道上完成两站点之间的可靠双向通信。实验中重点完成了帧格式设计、发送窗口与接收窗口管理、CRC32 差错检测、累计确认与逐帧确认、超时重传、NAK 辅助重传以及基于窗口的流量控制机制。

当前两份核心实现位于：

- [Lab1-Windows-VS2017/datalink_gobackn.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_gobackn.c:1)
- [Lab1-Windows-VS2017/datalink_selective.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_selective.c:1)

为便于验证，我在 `Lab1-linux` 环境下对上述两份 Windows 目录中的源码进行了交叉编译，并分别记录了无误码、默认误码率和高误码率下的实验结果。测试结果表明，Go-Back-N 在低误码场景下可以稳定工作，但在高误码场景下会因整窗回退而明显浪费带宽；Selective Repeat 则能够通过缓存乱序帧和单帧重传，在高误码条件下获得更好的吞吐率与信道利用率。

## 目录

1. 实验内容及目的
2. 实验环境
3. 协议设计
4. 软件设计
5. 关键参数的设计
6. 实验结果分析
7. 研究探索问题
8. 实验总结和心得体会
9. 个人分工说明
10. 附录

## 1 实验内容及目的

本实验要求利用数据链路层原理，在课程提供的仿真环境中实现一个滑动窗口协议，使其能够在有噪声信道上完成两站点之间的无差错双工通信。实验平台的固定条件如下：

- 信道带宽：`8000 bps`
- 单向传播时延：`270 ms`
- 默认误码率：`10^-5`
- 网络层分组长度：`256 B`
- 物理层向数据链路层提供帧发送与接收服务

本次实验完成了两种典型协议实现：

- Go-Back-N：接收端不缓存乱序帧，超时后从最早未确认帧开始整窗重传
- Selective Repeat：接收端缓存窗口内乱序帧，发送端只重传具体缺失或超时的单个帧

实验目的包括：

- 理解滑动窗口协议的数据传输流程
- 掌握发送窗口、接收窗口和序号回绕的处理方式
- 在实际代码中实现差错控制、重传和流量控制
- 比较 GBN 与 SR 在不同误码率下的性能差异

## 2 实验环境

本实验使用课程仓库中的三套工程：

- `Lab1-Windows-VS2017`：主要代码编写目录
- `Lab1-Windows-VS2013`：旧版 Visual Studio 工程
- `Lab1-linux`：便于命令行构建和自动化测试的 Linux 工程

本次报告涉及的主要文件如下：

- [Lab1-Windows-VS2017/datalink.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink.c:1)：课程原始停等版本
- [Lab1-Windows-VS2017/datalink_gobackn.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_gobackn.c:1)：Go-Back-N 实现
- [Lab1-Windows-VS2017/datalink_selective.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_selective.c:1)：Selective Repeat 实现
- [Lab1-Windows-VS2017/protocol.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/protocol.c:1)：协议仿真平台
- [Lab1-Windows-VS2017/protocol.h](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/protocol.h:1)：平台接口定义
- [Lab1-Windows-VS2017/datalink.h](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink.h:1)：帧类型与帧格式说明

开发与验证环境如下：

- Windows 侧目标环境：Visual Studio 2017 工程结构
- 验证环境：macOS + `gcc/clang`
- 编程语言：C
- 运行方式：事件驱动式双站点仿真

## 3 协议设计

### 3.1 协议分层结构及各层功能

实验平台按照典型的三层结构组织：

```text
图 3-1 协议分层结构示意

+-------------------------------+
|         Network Layer         |
| get_packet / put_packet       |
+-------------------------------+
|         Data Link Layer       |
| framing / ack / nak / timer   |
| window / retransmission       |
+-------------------------------+
|        Physical Layer         |
| send_frame / recv_frame       |
| delay / BER / full duplex     |
+-------------------------------+
```

各层功能如下：

- 网络层：按固定长度 `256 B` 产生分组，并接收链路层正确交付的数据
- 数据链路层：本实验需要自行实现的协议主体，负责成帧、确认、重传、差错检测和流量控制
- 物理层：由课程平台提供，负责模拟带宽、时延和误码率

### 3.2 程序总体结构

整个工程中，`protocol.c` 负责仿真环境，`datalink_*.c` 负责协议主体。总体流程如下：

1. 调用 `protocol_init()` 初始化网络层、物理层、套接字和定时器环境。
2. 数据链路层进入主事件循环。
3. 通过 `wait_for_event()` 等待事件驱动。
4. 根据不同事件执行发送、接收、确认、重传和网络层启停控制。

当前仓库中的三份链路层代码对应关系如下：

| 文件 | 协议类型 | 主要特征 |
| --- | --- | --- |
| `datalink.c` | 停等协议 | 一次只允许一个未确认帧 |
| `datalink_gobackn.c` | Go-Back-N | 接收端不缓存乱序帧，超时整窗回退 |
| `datalink_selective.c` | Selective Repeat | 接收端缓存乱序帧，发送端单帧重传 |

### 3.3 Go-Back-N 协议设计原理

Go-Back-N 的基本思想是：发送方可以在收到确认前连续发送多个帧，但接收方只接收“按序到达”的下一帧。如果中间某帧出错，后续帧即使正确到达也会被丢弃；发送方在超时后从最早未确认帧开始，把整个未确认窗口重新发送。

其关键状态量包括：

- `ack_expected`：发送窗口左边界
- `next_frame_to_send`：下一个新帧的序号
- `frame_expected`：接收端当前期待的序号
- `nbuffered`：窗口中已发送但未确认的帧数

GBN 的优点是实现简单，缺点是误码时浪费较多带宽。

### 3.4 Selective Repeat 协议设计原理

Selective Repeat 的基本思想是：发送方同样可以连续发送多个帧，但接收端会缓存窗口内的乱序帧；当缺失帧后来补到时，再把已经缓存的数据按序交付给网络层。发送端只重传超时或被 `NAK` 明确指出的某一帧，而不是把整个窗口全部重发。

本次实现中，SR 主要引入了以下额外状态：

- `too_far`：接收窗口右边界的下一位置
- `in_buf[]`：接收窗口缓存
- `arrived[]`：窗口内对应序号是否已经到达
- `acked[]`：发送窗口内对应序号是否已经被单独确认
- `no_nak`：防止对同一缺失位置反复发送 NAK

SR 的优点是在高误码条件下能有效减少重复发送；缺点是状态管理、缓存和确认逻辑更复杂。

### 3.5 GBN 与 SR 对比

| 维度 | Go-Back-N | Selective Repeat |
| --- | --- | --- |
| 接收端是否缓存乱序帧 | 否 | 是 |
| 超时后重传范围 | 整个未确认窗口 | 仅超时帧 |
| ACK 语义 | 以累计确认为主 | 逐帧确认为主 |
| 是否依赖 NAK | 可选 | 更适合结合 NAK |
| 误码率高时性能 | 较差 | 更好 |
| 实现复杂度 | 低 | 高 |

### 3.6 帧结构定义

数据链路层帧类型定义在 `datalink.h` 中，包括 `FRAME_DATA`、`FRAME_ACK` 和 `FRAME_NAK`。

```text
图 3-2 DATA 帧格式

+=========+========+========+===============+========+
| KIND(1) | ACK(1) | SEQ(1) |   DATA(256)   | CRC(4) |
+=========+========+========+===============+========+

图 3-3 ACK / NAK 帧格式

+=========+========+========+
| KIND(1) | ACK(1) | CRC(4) |
+=========+========+========+
```

字段含义如下：

- `KIND`：帧类型
- `SEQ`：当前数据帧的序号
- `ACK`：确认号；在 `NAK` 中表示请求重传的序号
- `DATA`：网络层分组
- `CRC`：CRC32 校验码

对当前实现而言：

- `DATA` 帧总长度为 `263 B`
- `ACK` / `NAK` 帧总长度为 `6 B`

### 3.7 实现可靠通信和误码控制

当前两种协议都依赖以下机制保证可靠性：

- 发送前对帧头和数据部分计算 CRC32
- 接收后重新校验 CRC，失败则丢弃
- 为未确认数据帧启动定时器
- 收到确认后停止对应定时器
- 根据窗口状态决定是否继续打开网络层

两者差异在于：

- GBN：接收端不缓存乱序帧，超时整窗重传
- SR：接收端缓存乱序帧，发送端仅重传具体帧，并可结合 NAK 加快恢复

## 4 软件设计

### 4.1 数据结构

#### 4.1.1 Go-Back-N 关键参数

```c
#define MAX_SEQ      7
#define WINDOW_SIZE  MAX_SEQ
#define DATA_TIMER   2000
```

含义如下：

- `MAX_SEQ = 7`：序号空间大小为 8
- `WINDOW_SIZE = 7`：GBN 窗口大小为 7
- `DATA_TIMER = 2000`：单帧超时重传时间

GBN 的发送缓存为：

```c
static unsigned char out_buf[MAX_SEQ + 1][PKT_LEN];
```

#### 4.1.2 Selective Repeat 关键参数

```c
#define MAX_SEQ      7
#define WINDOW_SIZE  ((MAX_SEQ + 1) / 2)
#define DATA_TIMER   2000
```

SR 的窗口取 `4`，满足“窗口大小不超过序号空间一半”的要求。其附加缓存与状态包括：

```c
static unsigned char out_buf[MAX_SEQ + 1][PKT_LEN];
static unsigned char acked[MAX_SEQ + 1];
static unsigned char in_buf[MAX_SEQ + 1][PKT_LEN];
static unsigned char arrived[MAX_SEQ + 1];
```

### 4.2 模块结构和算法思路

#### 4.2.1 `inc()`

该函数用于序号循环加一：

```c
*num = (*num + 1) % (MAX_SEQ + 1);
```

#### 4.2.2 `between()`

该函数用于判断序号 `b` 是否落在循环区间 `[a, c)` 中，是窗口滑动和序号回绕判断的核心辅助函数。

#### 4.2.3 `put_frame()`

该函数负责：

1. 在帧尾追加 CRC32
2. 调用 `send_frame()` 发往物理层
3. 将 `phl_ready` 置为 0

#### 4.2.4 `send_data_frame()`

该函数根据指定序号构造数据帧，并：

- 复制对应缓存中的网络层分组
- 填写 `seq` 和 `ack`
- 调用 `put_frame()`
- 启动该序号定时器

#### 4.2.5 `send_ack_frame()` 与 `send_nak_frame()`

这两个函数分别负责：

- 发送确认帧
- 发送重传请求帧

其中 `datalink_selective.c` 中的 `NAK` 表示“请重传指定序号帧”，而不是“请整窗回退”。

### 4.3 函数调用和程序流程

#### 4.3.1 函数调用图

```text
图 4-1 函数调用图

main
 ├─ protocol_init
 ├─ wait_for_event
 ├─ get_packet
 ├─ recv_frame
 ├─ put_packet
 ├─ send_data_frame / send_ack_frame / send_nak_frame
 │   └─ put_frame
 │      ├─ crc32
 │      └─ send_frame
 ├─ start_timer / stop_timer
 ├─ enable_network_layer
 └─ disable_network_layer
```

#### 4.3.2 程序流程图

```text
图 4-2 程序流程图

        +------------------+
        |  protocol_init   |
        +------------------+
                  |
                  v
        +------------------+
        | disable_network  |
        +------------------+
                  |
                  v
        +------------------+
        | wait_for_event   |
        +------------------+
                  |
      +-----------+------------+-------------+
      |                        |             |
      v                        v             v
 NETWORK_LAYER_READY   PHYSICAL_LAYER_READY  FRAME_RECEIVED / DATA_TIMEOUT
      |                        |             |
      v                        v             v
 发送新数据帧            phl_ready = 1    处理 ACK / DATA / NAK / 重传
      \_________________________|____________/
                           |
                           v
               +--------------------------+
               | 根据窗口和物理层状态决定 |
               | enable/disable network   |
               +--------------------------+
                           |
                           v
                    回到 wait_for_event
```

#### 4.3.3 Go-Back-N 核心流程

- 网络层就绪：取包、入发送缓存、发新帧
- 收到 ACK：累计确认，窗口可能连续滑动
- 收到 DATA：仅接受 `frame_expected`，其余乱序帧丢弃
- 数据超时：从 `ack_expected` 开始整窗重发

#### 4.3.4 Selective Repeat 核心流程

- 网络层就绪：取包、入发送缓存、发新帧
- 收到 ACK：仅标记具体序号已确认，再尝试滑动窗口
- 收到 DATA：
  - 若在接收窗口内且首次到达，则缓存
  - 立即回 ACK(该序号)
  - 若缺失帧导致乱序，则回 NAK(frame_expected)
  - 若窗口左边界形成连续数据，则按序交付
- 数据超时：仅重传具体超时帧

## 5 关键参数的设计

### 5.1 序列号大小 `MAX_SEQ`

当前两份实现均取 `MAX_SEQ = 7`，因此序号空间大小为 8。原因如下：

- 对 GBN，需要满足 `WINDOW_SIZE < MAX_SEQ + 1`
- 对 SR，需要满足 `WINDOW_SIZE <= (MAX_SEQ + 1) / 2`
- 较小序号空间便于调试和观察窗口回绕

### 5.2 窗口大小

GBN 采用：

```text
WINDOW_SIZE = 7
```

SR 采用：

```text
WINDOW_SIZE = 4
```

这样既满足协议正确性约束，也足以在当前信道条件下形成稳定流水。

### 5.3 重传定时时延 `DATA_TIMER`

当前两份实现均取 `2000 ms`。粗略估算单次往返时间：

- 单向传播时延：`270 ms`
- 数据帧发送时间：`263 B × 8 / 8000 bps ≈ 263 ms`
- ACK 帧发送时间：约 `6 ms`
- 反向传播时延：`270 ms`

因此一轮成功往返约为 `800 ms` 左右。将超时设置为 `2000 ms`，可以在避免误判的同时保证重传恢复不至于过慢。

### 5.4 ACK / NAK 策略

当前两份实现的策略不同：

- GBN：收到 DATA 后立即回 ACK，不使用独立 ACK 定时器
- SR：采用逐帧 ACK，并在乱序或 CRC 错误时发送 NAK 请求缺失帧重传

这种差异也导致两者在高误码场景下的恢复速度不同。

## 6 实验结果分析

### 6.1 实验结果概述

为了使结果与当前源码一致，我在 `Lab1-linux` 下分别交叉编译两份 Windows 目录中的协议文件：

```bash
gcc -O2 ../Lab1-Windows-VS2017/datalink_gobackn.c protocol.c lprintf.c crc32.c -I. -o gobackn_current -lm
gcc -O2 ../Lab1-Windows-VS2017/datalink_selective.c protocol.c lprintf.c crc32.c -I. -o selective_current -lm
```

测试场景包括：

- 无误码信道 `-u`
- 默认误码率 `10^-5`
- 高误码率 `10^-4`

### 6.2 理论性能分析

对当前帧格式而言，单个数据帧的有效载荷占比为：

```text
η_frame = 256 / 263 ≈ 97.34%
```

这给出了理想情况下的理论上限。实际运行时，性能还会受到如下因素影响：

- 事件驱动调度开销
- ACK / NAK 控制帧开销
- 窗口起始填充阶段
- 误码导致的重传行为

因此实测利用率略低于理论上限是正常现象。

### 6.3 Go-Back-N 测试结果

| 测试条件 | 站点 | 结束时接收包数 | 吞吐率 | 利用率 | 误码计数 |
| --- | --- | --- | --- | --- | --- |
| `-f -u -t 12` | A | 40 | 7572 bps | 94.65% | 0 |
| `-f -u -t 12` | B | 40 | 7562 bps | 94.53% | 0 |
| `-f -t 20` | A | 56 | 6280 bps | 78.50% | 1 |
| `-f -t 20` | B | 54 | 6244 bps | 78.04% | 1 |
| `-f -b 1e-4 -t 12` | A | 9 | 2320 bps | 29.00% | 6 |
| `-f -b 1e-4 -t 12` | B | 17 | 3446 bps | 43.08% | 5 |

按双向平均值统计：

| 场景 | 平均吞吐率 | 平均利用率 |
| --- | --- | --- |
| 无误码 | 7567 bps | 94.59% |
| 默认误码率 `10^-5` | 6262 bps | 78.27% |
| 高误码率 `10^-4` | 2883 bps | 36.04% |

### 6.4 Selective Repeat 测试结果

| 测试条件 | 站点 | 结束时接收包数 | 吞吐率 | 利用率 | 误码计数 |
| --- | --- | --- | --- | --- | --- |
| `-f -u -t 10` | A | 32 | 7506 bps | 93.83% | 0 |
| `-f -u -t 10` | B | 32 | 7499 bps | 93.74% | 0 |
| `-f -t 15` | A | 48 | 7510 bps | 93.88% | 0 |
| `-f -t 15` | B | 48 | 7510 bps | 93.88% | 1 |
| `-f -b 1e-4 -t 12` | A | 22 | 4816 bps | 60.20% | 6 |
| `-f -b 1e-4 -t 12` | B | 17 | 4214 bps | 52.67% | 4 |

按双向平均值统计：

| 场景 | 平均吞吐率 | 平均利用率 |
| --- | --- | --- |
| 无误码 | 7503 bps | 93.79% |
| 默认误码率 `10^-5` | 7510 bps | 93.88% |
| 高误码率 `10^-4` | 4515 bps | 56.44% |

### 6.5 两种协议的性能对比

| 场景 | GBN 平均利用率 | SR 平均利用率 | 结论 |
| --- | --- | --- | --- |
| 无误码 | 94.59% | 93.79% | 两者都接近信道上限，GBN 略高 |
| 默认误码率 `10^-5` | 78.27% | 93.88% | 本次样本中 SR 更稳定，说明乱序缓存与单帧确认起效 |
| 高误码率 `10^-4` | 36.04% | 56.44% | SR 明显优于 GBN，误码越高优势越明显 |

从实验结果可以得到：

1. 在低误码场景下，两者都可以较充分利用信道。
2. GBN 在出现差错时会整窗重传，因此在高误码下性能下降更明显。
3. SR 通过缓存乱序帧与单帧重传，能更有效减少重复发送。

### 6.6 运行展示说明

完整版往届报告在这一部分通常会插入终端运行截图或性能测试记录表图片。当前 Markdown 中已补齐文字和表格，但导出 PDF 前建议再插入以下截图：

- `GBN --utopia` 终端截图
- `GBN --flood` 终端截图
- `GBN --ber=1e-4` 终端截图
- `SR --utopia` 终端截图
- `SR --ber=1e-4` 终端截图

若老师不强制要求图片版，此处表格与附录日志摘录已足以说明实验结果。

## 7 研究探索问题

### 7.1 CRC 校验能力

CRC32 具有较强的差错检测能力，能够检测：

- 全部单比特错误
- 大部分突发错误
- 多种常见随机错误模式

在本实验中，CRC 的任务是“发现错误并丢弃损坏帧”，而不是直接纠错。真正的可靠性仍由 ARQ 协议保证。

### 7.2 CRC 校验和的计算方法

`crc32.c` 采用查表法完成 CRC32 计算。其核心思想是：把按位多项式除法中大量重复的部分预先计算成固定查找表，运行时按字节更新 CRC 寄存器，从而显著提升效率。

在本实验中：

- 发送前：对帧头和数据区计算 CRC，并写入帧尾
- 接收后：对整个帧重新计算 CRC，结果为 0 则视为通过

### 7.3 程序设计方面的问题

实现过程中最关键的设计点包括：

- GBN 中 `between()` 的正确性直接影响窗口回绕后的确认逻辑
- SR 中接收缓存与 `arrived[]` 的管理必须与窗口滑动同步
- 若 ACK 或 NAK 的语义定义不清晰，很容易出现重复重传或窗口不滑动

本次实现中，GBN 与 SR 采用不同的确认思想：

- GBN 偏向累计确认
- SR 偏向逐帧确认，并结合 NAK 提示具体缺失帧

### 7.4 软件测试方面的问题

短时测试容易受到随机误码波动影响，因此：

- 不能只看单次瞬时吞吐率
- 需要同时结合错误次数和运行日志判断协议行为
- A、B 两侧结果在有限时间内可能并不完全对称

例如本次 `1e-4` 场景中，GBN 和 SR 两个方向的误码计数并不完全相同，但总体趋势仍然清晰：SR 的高误码表现更优。

### 7.5 对等协议实体之间的流量控制

当前两份实现都使用“发送窗口大小”作为基本流量控制手段。只有在以下条件同时满足时，才允许网络层继续交付新分组：

- `nbuffered < WINDOW_SIZE`
- `phl_ready == 1`

因此窗口机制本身就兼具了：

- 未确认帧管理
- 发送速率限制

### 7.6 与标准协议的对比

课程实验中的协议属于教学化简模型，与真实链路层协议相比仍有明显差距：

- 不涉及面向比特的透明传输与位填充
- 未实现链路建立、链路探测和链路拆除
- 未考虑复杂的链路状态维护

但它仍保留了 HDLC / LAPB 等成熟协议中的核心思想：

- 序号编号
- 帧确认
- 定时器
- 重传机制
- CRC 差错检测
- 滑动窗口

因此，本实验非常适合作为理解真实链路层协议的入门模型。

## 8 实验总结和心得体会

### 8.1 实验总结

本次实验共完成了两份滑动窗口协议实现：

- `datalink_gobackn.c`
- `datalink_selective.c`

其中：

- GBN 代码更简洁，适合理解累计确认和整窗重传的基本流程
- SR 代码状态更多，但能更充分体现乱序缓存和单帧重传的优势

从实验结果看：

- 两种协议在低误码场景下都能接近信道上限运行
- 随着误码率上升，SR 的优势会更明显

这说明协议设计中的“重传粒度”和“接收端是否缓存乱序帧”对性能有直接决定作用。

### 8.2 心得体会

通过本实验，能够明显体会到“协议正确性”和“协议性能”是两个不同层次的问题：

- 正确性要求帧格式、窗口边界、序号回绕和定时器逻辑完全一致
- 性能则进一步取决于窗口大小、误码恢复方式、确认策略和实现细节

自己动手实现之后，会比课堂上只看理论时更深刻地理解：

- 为什么 GBN 高误码场景下会明显掉速
- 为什么 SR 能减少无意义重发
- 为什么滑动窗口协议必须严格限制序号空间与窗口大小的关系

## 9 个人分工说明

若按个人提交形式整理，可写为：

> 本实验由本人独立完成。主要工作包括阅读实验指导书与平台代码，分析 `protocol.c` 与 `protocol.h` 提供的事件驱动接口，完成 `datalink_gobackn.c` 与 `datalink_selective.c` 的设计和调试，交叉编译并记录实验数据，最终撰写实验报告。

若课程要求按真实完成情况填写，请替换为你的实际分工说明。

## 10 附录

### 附录 1 源码文件清单

- [Lab1-Windows-VS2017/datalink_gobackn.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_gobackn.c:1)
- [Lab1-Windows-VS2017/datalink_selective.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_selective.c:1)
- [Lab1-Windows-VS2017/protocol.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/protocol.c:1)
- [Lab1-Windows-VS2017/protocol.h](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/protocol.h:1)
- [Lab1-Windows-VS2017/datalink.h](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink.h:1)

若老师要求“完整源代码必须跟在 PDF 后”，建议导出前把 `datalink_selective.c` 和 `datalink_gobackn.c` 全文追加到附录末尾。

### 附录 2 复现实验命令

#### GBN

```bash
gcc -O2 ../Lab1-Windows-VS2017/datalink_gobackn.c protocol.c lprintf.c crc32.c -I. -o gobackn_current -lm
```

```bash
./gobackn_current -f -u -t 12 -p 59202 A
./gobackn_current -f -u -t 12 -p 59202 B
```

```bash
./gobackn_current -f -t 20 -p 59204 A
./gobackn_current -f -t 20 -p 59204 B
```

```bash
./gobackn_current -f -b 1e-4 -t 12 -p 59203 A
./gobackn_current -f -b 1e-4 -t 12 -p 59203 B
```

#### SR

```bash
gcc -O2 ../Lab1-Windows-VS2017/datalink_selective.c protocol.c lprintf.c crc32.c -I. -o selective_current -lm
```

```bash
./selective_current -f -u -t 10 -p 59231 A
./selective_current -f -u -t 10 -p 59231 B
```

```bash
./selective_current -f -t 15 -p 59232 A
./selective_current -f -t 15 -p 59232 B
```

```bash
./selective_current -f -b 1e-4 -t 12 -p 59233 A
./selective_current -f -b 1e-4 -t 12 -p 59233 B
```

### 附录 3 协议工作过程展示

#### 3.1 GBN 无误码运行摘录

```text
002.675 .... 8 packets received, 7606 bps, 95.08%, Err 0
004.871 .... 16 packets received, 7533 bps, 94.16%, Err 0
007.911 .... 18 packets received, 4988 bps, 62.35%, Err 1
012.228 .... 34 packets received, 5948 bps, 74.35%, Err 1
018.784 .... 56 packets received, 6280 bps, 78.50%, Err 1
```

#### 3.2 GBN 高误码运行摘录

```text
003.745 .... 1 packets received, 731 bps, 9.14%, Err 4
005.886 .... 9 packets received, 3730 bps, 46.63%, Err 4
007.993 .... 15 packets received, 4359 bps, 54.48%, Err 4
011.047 .... 17 packets received, 3446 bps, 43.08%, Err 5
```

#### 3.3 SR 无误码运行摘录

```text
002.779 .... 8 packets received, 7502 bps, 93.77%, Err 0
004.970 .... 16 packets received, 7492 bps, 93.64%, Err 0
007.152 .... 24 packets received, 7496 bps, 93.70%, Err 0
009.326 .... 32 packets received, 7506 bps, 93.83%, Err 0
```

#### 3.4 SR 高误码运行摘录

```text
003.734 .... 8 packets received, 5088 bps, 63.60%, Err 3
006.857 .... 16 packets received, 5166 bps, 64.58%, Err 5
009.869 .... 22 packets received, 4816 bps, 60.20%, Err 6
```

#### 3.5 截图补充建议

若导出 PDF 前还希望让版式更接近往届完整版，建议再补以下图片：

- Visual Studio 中 `datalink_gobackn.c` 关键代码截图
- Visual Studio 中 `datalink_selective.c` 关键代码截图
- GBN 三组场景运行截图
- SR 两组场景运行截图
- 性能测试记录表图片或扫描件

## 目录

摘要

1. 实验内容及目的
2. 实验环境
3. 协议设计

	3.1 协议分层结构及各层功能
	
	3.2 程序总体结构
	
	3.3 Go-Back-N 协议设计原理
	
	3.4 Selective Repeat 协议设计原理
	
	3.5 GBN 与 SR 对比
	
	3.6 帧结构定义
	
	3.7 实现可靠通信和误码控制

  
4. 软件设计

	4.1 数据结构
	
	4.2 模块结构和算法思路
	
	4.3 函数调用和程序流程


5. 关键参数的设计

	5.1 序列号大小 `MAX_SEQ`
	
	5.2 窗口大小
	
	5.3 重传定时时延 `DATA_TIMER`
	
	5.4 ACK / NAK 策略


6. 实验结果分析

	6.1 实验结果概述
	
	6.2 理论性能分析
	
	6.3 Go-Back-N 测试结果
	
	6.4 Selective Repeat 测试结果
	
	6.5 两种协议的性能对比
	
	6.6 运行展示说明

7. 研究探索问题

	7.1 CRC 校验能力
	
	7.2 CRC 校验和的计算方法
	
	7.3 程序设计方面的问题
	
	7.4 软件测试方面的问题
	
	7.5 对等协议实体之间的流量控制
	
	7.6 与标准协议的对比

  
8. 实验总结和心得体会

	8.1 实验总结
	
	8.2 心得体会

9. 个人分工说明
  
10. 附录

	附录 1 完整源码（精简注释版）
	
	附录 2 复现实验命令
	
	附录 3 协议工作过程展示


## 摘要

本实验基于课程提供的数据链路层仿真平台，分别实现了 Go-Back-N 与 Selective Repeat 两种滑动窗口协议，用于在带宽为 `8000 bps`、单向传播时延为 `270 ms`、默认误码率为 `10^-5` 的全双工卫星信道上完成两站点之间的可靠双向通信。实验中重点完成了帧格式设计、发送窗口与接收窗口管理、CRC32 差错检测、累计确认与逐帧确认、超时重传、NAK 辅助重传以及基于窗口的流量控制机制。

当前两份核心实现位于：

- [Lab1-Windows-VS2017/datalink_gobackn.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_gobackn.c:1)

- [Lab1-Windows-VS2017/datalink_selective.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-Windows-VS2017/datalink_selective.c:1)

为便于展示和验收，最终在 `Lab1-linux` 目录下分别维护了 `datalink_gobackn.c` 与 `datalink_selective.c` 两份可直接构建的协议实现，并补充了统一的 `Makefile` 用于自动化编译与运行六组标准测试场景。实验结果表明，Go-Back-N 在低误码场景下可以稳定工作，但在高误码场景下会因整窗回退而明显浪费带宽；Selective Repeat 则能够通过缓存乱序帧和单帧重传，在高误码条件下获得更好的吞吐率与信道利用率。

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
- `Lab1-linux`：当前实际展示与验收使用的 Linux 工程，已加入统一 `Makefile`

本次报告涉及的主要文件如下：

- [Lab1-linux/datalink.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-linux/datalink.c:1)：课程原始停等版本
- [Lab1-linux/datalink_gobackn.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-linux/datalink_gobackn.c:1)：Go-Back-N 实现
- [Lab1-linux/datalink_selective.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-linux/datalink_selective.c:1)：Selective Repeat 实现
- [Lab1-linux/protocol.c](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-linux/protocol.c:1)：协议仿真平台
- [Lab1-linux/protocol.h](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-linux/protocol.h:1)：平台接口定义
- [Lab1-linux/datalink.h](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-linux/datalink.h:1)：帧类型与帧格式说明
- [Lab1-linux/Makefile](/Users/reiayanami/Project/computernet-lab1/bupt-computernet-lab1/Lab1-linux/Makefile:1)：自动化编译与六组标准测试入口


开发与验证环境如下：

- 代码参考来源：Visual Studio 2017 工程结构
- 实际展示与验收环境：`Lab1-linux` + `gcc/clang` + `make`
- 编程语言：C
- 运行方式：事件驱动式双站点仿真；`Makefile` 在同一终端中后台启动 `A` 端、前台启动 `B` 端完成成对测试

## 3 协议设计

### 3.1 协议分层结构及各层功能

实验平台按照典型的三层结构组织：
![[Pasted image 20260515135122.png]]
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

| 文件                     | 协议类型             | 主要特征             |
| ---------------------- | ---------------- | ---------------- |
| `datalink.c`           | 停等协议             | 一次只允许一个未确认帧      |
| `datalink_gobackn.c`   | Go-Back-N        | 接收端不缓存乱序帧，超时整窗回退 |
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

| 维度         | Go-Back-N | Selective Repeat |
| ---------- | --------- | ---------------- |
| 接收端是否缓存乱序帧 | 否         | 是                |
| 超时后重传范围    | 整个未确认窗口   | 仅超时帧             |
| ACK 语义     | 以累计确认为主   | 逐帧确认为主           |
| 是否依赖 NAK   | 可选        | 更适合结合 NAK        |
| 误码率高时性能    | 较差        | 更好               |
| 实现复杂度      | 低         | 高                |

### 3.6 帧结构定义
数据链路层帧类型定义在 `datalink.h` 中，包括 `FRAME_DATA`、`FRAME_ACK` 和 `FRAME_NAK`。

```text

图 3-2 DATA 帧格式

+=========+========+========+===============+========+

| KIND(1) | ACK(1) | SEQ(1) | DATA(256) | CRC(4) |

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
#define MAX_SEQ 7

#define WINDOW_SIZE MAX_SEQ

#define DATA_TIMER 2000
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
#define MAX_SEQ 7

#define WINDOW_SIZE ((MAX_SEQ + 1) / 2)

#define DATA_TIMER 2000
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

![[Pasted image 20260515135402.png]]
#### 4.3.2 程序流程图
![[Pasted image 20260515135426.png]]
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

当前两份实现均取 `2000 ms`。粗略估算单次往返时间

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

为了使结果与当前展示环境一致，本次实验直接在 `Lab1-linux` 目录下构建并运行协议实现。为提高演示效率，还补充了统一 `Makefile`，可自动完成 `GBN/SR` 编译以及六组标准测试场景的启动。

```bash
make build
```

其中：

- `make build`：同时构建 `gbn_exe` 与 `sr_exe`
- `make gbn-utopia / gbn-default / gbn-high-ber`：运行 `GBN` 三组标准场景
- `make sr-utopia / sr-default / sr-high-ber`：运行 `SR` 三组标准场景

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

| 测试条件               | 站点  | 结束时接收包数 | 吞吐率      | 利用率    | 误码计数 |
| ------------------ | --- | ------- | -------- | ------ | ---- |
| `-f -u -t 12`      | A   | 40      | 7572 bps | 94.65% | 0    |
| `-f -u -t 12`      | B   | 40      | 7562 bps | 94.53% | 0    |
| `-f -t 20`         | A   | 56      | 6280 bps | 78.50% | 1    |
| `-f -t 20`         | B   | 54      | 6244 bps | 78.04% | 1    |
| `-f -b 1e-4 -t 12` | A   | 9       | 2320 bps | 29.00% | 6    |
| `-f -b 1e-4 -t 12` | B   | 17      | 3446 bps | 43.08% | 5    |

按双向平均值统计：

| 场景            | 平均吞吐率    | 平均利用率  |
| ------------- | -------- | ------ |
| 无误码           | 7567 bps | 94.59% |
| 默认误码率 `10^-5` | 6262 bps | 78.27% |
| 高误码率 `10^-4`  | 2883 bps | 36.04% |

### 6.4 Selective Repeat 测试结果

| 测试条件               | 站点  | 结束时接收包数 | 吞吐率      | 利用率    | 误码计数 |
| ------------------ | --- | ------- | -------- | ------ | ---- |
| `-f -u -t 10`      | A   | 32      | 7506 bps | 93.83% | 0    |
| `-f -u -t 10`      | B   | 32      | 7499 bps | 93.74% | 0    |
| `-f -t 15`         | A   | 48      | 7510 bps | 93.88% | 0    |
| `-f -t 15`         | B   | 48      | 7510 bps | 93.88% | 1    |
| `-f -b 1e-4 -t 12` | A   | 22      | 4816 bps | 60.20% | 6    |
| `-f -b 1e-4 -t 12` | B   | 17      | 4214 bps | 52.67% | 4    |


按双向平均值统计：

| 场景            | 平均吞吐率    | 平均利用率  |
| ------------- | -------- | ------ |
| 无误码           | 7503 bps | 93.79% |
| 默认误码率 `10^-5` | 7510 bps | 93.88% |
| 高误码率 `10^-4`  | 4515 bps | 56.44% |
### 6.5 两种协议的性能对比

| 场景            | GBN 平均利用率 | SR 平均利用率 | 结论                         |
| ------------- | --------- | -------- | -------------------------- |
| 无误码           | 94.59%    | 93.79%   | 两者都接近信道上限，GBN 略高           |
| 默认误码率 `10^-5` | 78.27%    | 93.88%   | 本次样本中 SR 更稳定，说明乱序缓存与单帧确认起效 |
| 高误码率 `10^-4`  | 36.04%    | 56.44%   | SR 明显优于 GBN，误码越高优势越明显      |


从实验结果可以得到：

1. 在低误码场景下，两者都可以较充分利用信道。

2. GBN 在出现差错时会整窗重传，因此在高误码下性能下降更明显。

3. SR 通过缓存乱序帧与单帧重传，能更有效减少重复发送。

### 6.6 运行展示说明

完整版往届报告在这一部分通常会插入终端运行截图或性能测试记录表图片。当前 Markdown 中已补齐文字和表格，但导出 PDF 前建议再插入以下截图：

- gbn无误码运行结果展示
![[Pasted image 20260515132533.png]]

- gbn默认误码运行展示
![[Pasted image 20260515132248.png]]

  - gbn高误码运行展示
  ![[Pasted image 20260515132814.png]]
- sr无误码运行结果展示
![[Pasted image 20260515133218.png|682]]
- sr默认误码率运行结果展示
![[Pasted image 20260515133352.png]]
- sr高误码率运行结果展示
![[Pasted image 20260515133512.png]]
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

本实验由王哲、黄严、李俊泉三位同学协作完成，分工如下：

- 王哲：承担主要工作。负责整体方案设计与任务统筹，完成 `Go-Back-N` 与 `Selective Repeat` 两种协议的核心实现，重点处理发送窗口、接收窗口、序号回绕、ACK/NAK 逻辑、超时重传与缓存管理等关键模块；同时负责主要调试、`Lab1-linux` 运行架构整理、`Makefile` 自动化构建与测试入口设计、实验数据整理以及报告主体撰写。

- 黄严：负责实验平台与接口层面的辅助分析，参与 `protocol.c`、`protocol.h`、`datalink.h` 的阅读和实验环境梳理；协助完成测试命令整理、运行结果记录、部分结果分析以及报告中实验环境与运行方式相关内容的补充。

- 李俊泉：负责资料整理与结果复核，参与协议流程图、实现思路和对比分析的整理；协助检查 `GBN` 与 `SR` 在不同误码率场景下的输出结果，补充附录、源码排版以及报告格式细节的校对。

## 10 附录

### 附录 1 完整源码（精简注释版）

#### 附录 1.1 GBN 源码

```c

#include <stdio.h>

#include <string.h>

#include "protocol.h"

#include "datalink.h"

/* GBN：累计确认、按序接收、整窗重传。 */

#define MAX_SEQ 7

#define WINDOW_SIZE MAX_SEQ

#define DATA_TIMER 2000

  

struct FRAME {

unsigned char kind;

unsigned char ack;

unsigned char seq;

unsigned char data[PKT_LEN];

unsigned int padding;

};

/* 发送窗口：[ack_expected, next_frame_to_send)。 */

static unsigned char ack_expected = 0;

static unsigned char next_frame_to_send = 0;

static unsigned char nbuffered = 0;

  

/* 接收端只接收当前按序到达的下一帧。 */

static unsigned char frame_expected = 0;


static unsigned char out_buf[MAX_SEQ + 1][PKT_LEN];

  
/* 物理层当前是否还能继续发送一帧。 */

static int phl_ready = 0;


static void inc(unsigned char *num)

{

*num = (*num + 1) % (MAX_SEQ + 1);

}


/* 判断 b 是否落在循环区间 [a, c) 内。 */

static int between(unsigned char a, unsigned char b, unsigned char c)

{

return ((a <= b) && (b < c)) ||

((c < a) && (a <= b)) ||

((b < c) && (c < a));

}

  
/* 确认最近一个已按序交付给网络层的帧。 */

static unsigned char latest_ack(void)

{

return (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

}
 

/* 追加 CRC，然后交给实验平台发送。 */

static void put_frame(unsigned char *frame, int len)

{

*(unsigned int *)(frame + len) = crc32(frame, len);

send_frame(frame, len + 4);

phl_ready = 0;

}

  
/* 发送一个 DATA 帧，并启动该帧定时器。 */

static void send_data_frame(unsigned char frame_nr)

{

struct FRAME s;
  

s.kind = FRAME_DATA;

s.seq = frame_nr;

s.ack = latest_ack();

memcpy(s.data, out_buf[frame_nr], PKT_LEN);

  

dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);
 

put_frame((unsigned char *)&s, 3 + PKT_LEN);

start_timer(frame_nr, DATA_TIMER);

}


/* 当前没有捎带数据时，发送纯 ACK 帧。 */

static void send_ack_frame(void)

{

struct FRAME s;

  

s.kind = FRAME_ACK;

s.ack = latest_ack();

  
dbg_frame("Send ACK %d\n", s.ack);

  
put_frame((unsigned char *)&s, 2);

}
 

/* GBN 超时规则：重传整个未确认窗口。 */

static void resend_window(void)

{

unsigned char i;

unsigned char frame_nr;


frame_nr = ack_expected;

for (i = 0; i < nbuffered; i++) {

send_data_frame(frame_nr);

inc(&frame_nr);

}

}

  
int main(int argc, char **argv)

{

int event, arg;

struct FRAME f;

int len = 0;

  
protocol_init(argc, argv);

lprintf("Go-Back-N sliding window, build: " __DATE__" "__TIME__"\n");

lprintf("MAX_SEQ=%d, WINDOW_SIZE=%d\n", MAX_SEQ, WINDOW_SIZE);

disable_network_layer();

for (;;) {

event = wait_for_event(&arg);


switch (event) {

case NETWORK_LAYER_READY:

/* 取一个分组，放入缓存，发送后推进发送窗口右边界。 */


get_packet(out_buf[next_frame_to_send]);

nbuffered++;

send_data_frame(next_frame_to_send);

inc(&next_frame_to_send);

break;


case PHYSICAL_LAYER_READY:

/* 物理层重新提供了一次发送机会。 */


phl_ready = 1;

break;

  

case FRAME_RECEIVED:

/* 收到一帧，先校验 CRC，再处理其中的 DATA/ACK 影响。 */


len = recv_frame((unsigned char *)&f, sizeof f);
 

if (len < 5 || crc32((unsigned char *)&f, len) != 0) {

dbg_event("**** Receiver Error, Bad CRC Checksum\n");

break;

}

if (f.kind == FRAME_ACK)

dbg_frame("Recv ACK %d\n", f.ack);


if (f.kind == FRAME_DATA) {

/* DATA 更新接收状态；其中捎带的 ACK 更新发送状态。 */

dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);

  
if (f.seq == frame_expected) {

put_packet(f.data, len - 7);

inc(&frame_expected);

}

send_ack_frame();

}


/* 累计 ACK 可能一次推动窗口跨过多个帧。 */

while (nbuffered > 0 && between(ack_expected, f.ack, next_frame_to_send)) {

nbuffered--;

stop_timer(ack_expected);

inc(&ack_expected);

}

break;


case DATA_TIMEOUT:

/* 任一帧超时，都要把当前窗口整段重发。 */


dbg_event("---- DATA %d timeout\n", arg);

resend_window();

break;

}

  
/* 仅当发送窗口未满时，才重新打开网络层。 */

if (nbuffered < WINDOW_SIZE && phl_ready)

enable_network_layer();

else

disable_network_layer();

}

}

```

#### 附录 1.2 SR 源码

```c

#include <stdio.h>

#include <string.h>

  

#include "protocol.h"

#include "datalink.h"

  

/* SR：乱序缓存、逐帧确认、逐帧重传。 */

  

#define MAX_SEQ 7

  

#define WINDOW_SIZE ((MAX_SEQ + 1) / 2)

#define DATA_TIMER 2000

  

struct FRAME {

unsigned char kind;

unsigned char ack;

unsigned char seq;

unsigned char data[PKT_LEN];

unsigned int padding;

};

  

/* 发送窗口，以及逐帧确认状态。 */

static unsigned char ack_expected = 0;

static unsigned char next_frame_to_send = 0;

static unsigned char nbuffered = 0;

static unsigned char out_buf[MAX_SEQ + 1][PKT_LEN];

static unsigned char acked[MAX_SEQ + 1];

  

/* 接收窗口：[frame_expected, too_far)。 */

static unsigned char frame_expected = 0;

static unsigned char too_far = WINDOW_SIZE;

static unsigned char in_buf[MAX_SEQ + 1][PKT_LEN];

static unsigned char arrived[MAX_SEQ + 1];

/* 避免对同一个缺口重复发送 NAK。 */

static int no_nak = 1;

  

/* 物理层当前是否还能继续发送一帧。 */

static int phl_ready = 0;

  

static void inc(unsigned char *num)

{

*num = (*num + 1) % (MAX_SEQ + 1);

}

  

/* 判断 b 是否落在循环区间 [a, c) 内。 */

static int between(unsigned char a, unsigned char b, unsigned char c)

{

return ((a <= b) && (b < c)) ||

((c < a) && (a <= b)) ||

((b < c) && (c < a));

}

  

/* 判断某帧是否属于当前接收窗口。 */

static int in_receive_window(unsigned char seq)

{

return between(frame_expected, seq, too_far);

}

  

/* 判断某帧是否是上一窗口中的旧重复帧。 */

static int in_previous_window(unsigned char seq)

{

unsigned char low = (frame_expected + MAX_SEQ + 1 - WINDOW_SIZE) % (MAX_SEQ + 1);

  

return between(low, seq, frame_expected);

}

  

/* 追加 CRC，然后交给实验平台发送。 */

static void put_frame(unsigned char *frame, int len)

{

*(unsigned int *)(frame + len) = crc32(frame, len);

send_frame(frame, len + 4);

phl_ready = 0;

}

  

/* 确认某一个具体序号的帧。 */

static void send_ack_frame(unsigned char ack_nr)

{

struct FRAME s;

  

s.kind = FRAME_ACK;

s.ack = ack_nr;

  

dbg_frame("Send ACK %d\n", s.ack);

  

put_frame((unsigned char *)&s, 2);

}

  

/* 请求对方重传当前缺失的那一帧。 */

static void send_nak_frame(unsigned char nak_nr)

{

struct FRAME s;

  

s.kind = FRAME_NAK;

s.ack = nak_nr;

no_nak = 0;

  

dbg_frame("Send NAK %d\n", s.ack);

  

put_frame((unsigned char *)&s, 2);

}

  

/* 发送一个 DATA 帧，并启动它自己的定时器。 */

static void send_data_frame(unsigned char frame_nr)

{

struct FRAME s;

  

s.kind = FRAME_DATA;

s.seq = frame_nr;

  

s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);

memcpy(s.data, out_buf[frame_nr], PKT_LEN);

  

dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

  

put_frame((unsigned char *)&s, 3 + PKT_LEN);

start_timer(frame_nr, DATA_TIMER);

}

  

/* 标记某帧已确认，并在可能时推动窗口左边界前移。 */

static void mark_acked(unsigned char seq)

{

if (between(ack_expected, seq, next_frame_to_send) && !acked[seq]) {

acked[seq] = 1;

stop_timer(seq);

}

  

while (nbuffered > 0 && acked[ack_expected]) {

acked[ack_expected] = 0;

nbuffered--;

inc(&ack_expected);

}

}

  

int main(int argc, char **argv)

{

int event, arg;

struct FRAME f;

int len = 0;

  

protocol_init(argc, argv);

lprintf("Selective Repeat sliding window, build: " __DATE__" "__TIME__"\n");

lprintf("MAX_SEQ=%d, WINDOW_SIZE=%d\n", MAX_SEQ, WINDOW_SIZE);

  

disable_network_layer();

  

for (;;) {

event = wait_for_event(&arg);

  

switch (event) {

case NETWORK_LAYER_READY:

/* 取一个分组，放入缓存，发送后推进发送窗口右边界。 */

  

get_packet(out_buf[next_frame_to_send]);

acked[next_frame_to_send] = 0;

nbuffered++;

send_data_frame(next_frame_to_send);

inc(&next_frame_to_send);

break;

  

case PHYSICAL_LAYER_READY:

/* 物理层重新提供了一次发送机会。 */

phl_ready = 1;

break;

  

case FRAME_RECEIVED:

/* 收到一帧，先校验 CRC，再处理 ACK/NAK/DATA。 */

len = recv_frame((unsigned char *)&f, sizeof f);

if (len < 5 || crc32((unsigned char *)&f, len) != 0) {

dbg_event("**** Receiver Error, Bad CRC Checksum\n");

  

if (no_nak)

send_nak_frame(frame_expected);

break;

}

  

switch (f.kind) {

case FRAME_ACK:

/* 发送端逻辑：标记这一帧已经被确认。 */

dbg_frame("Recv ACK %d\n", f.ack);

  

mark_acked(f.ack);

break;

  

case FRAME_NAK:

/* 发送端逻辑：只重传被请求的那一帧。 */

dbg_frame("Recv NAK %d\n", f.ack);

  

if (between(ack_expected, f.ack, next_frame_to_send) && !acked[f.ack])

send_data_frame(f.ack);

break;

  

case FRAME_DATA:

/* 既处理接收端的 DATA，也处理捎带回来的 ACK。 */

dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);

  
  

mark_acked(f.ack);

  

if (in_receive_window(f.seq)) {

/* 窗口内帧先缓存、立即确认，再按序交付。 */

  

if (f.seq != frame_expected && no_nak)

send_nak_frame(frame_expected);

  

if (!arrived[f.seq]) {

arrived[f.seq] = 1;

memcpy(in_buf[f.seq], f.data, len - 7);

}

  
  

send_ack_frame(f.seq);

  
  

/* 只要形成连续按序的一段，就立即连续交付。 */

while (arrived[frame_expected]) {

put_packet(in_buf[frame_expected], len - 7);

arrived[frame_expected] = 0;

no_nak = 1;

inc(&frame_expected);

inc(&too_far);

}

} else if (in_previous_window(f.seq)) {

/* 旧重复帧不再交付，只重复发送 ACK。 */

  

send_ack_frame(f.seq);

}

break;

}

break;

  

case DATA_TIMEOUT:

/* SR 超时规则：只重传当前超时的这一帧。 */

dbg_event("---- DATA %d timeout\n", arg);

  

if (!acked[arg])

send_data_frame((unsigned char)arg);

break;

}

  
  

/* 仅当发送窗口未满时，才重新打开网络层。 */

if (nbuffered < WINDOW_SIZE && phl_ready)

enable_network_layer();

else

disable_network_layer();

}

}

```

### 附录 2 复现实验命令

统一先在 `Lab1-linux` 目录执行：

```bash
make build
```
#### GBN
```bash
make gbn-utopia
```
  
```bash
make gbn-default
```

```bash
make gbn-high-ber
```
#### SR

```bash
make sr-utopia
```

```bash
make sr-default
```

```bash
make sr-high-ber
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
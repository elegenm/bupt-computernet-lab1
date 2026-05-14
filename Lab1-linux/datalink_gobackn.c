#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/*
 * Go-Back-N 滑动窗口版本。
 *
 * 这份代码相对 datalink.c 的“停等协议”做了两处核心扩展：
 * 1. 发送端允许在收到确认前连续发送多个 DATA 帧；
 * 2. 接收端仍然只接受“按序到达”的下一帧，乱序帧直接丢弃。
 *
 * 因此，链路上可以同时存在多个未确认帧，提高吞吐量；
 * 但一旦某一帧丢失或损坏，发送端会从最早未确认帧开始，
 * 把后续已经发出的帧一并重传，这正是 Go-Back-N 的特征。
 *
 * 原 datalink.c 保持不变，本文件可单独替换进工程编译。
 */

#define MAX_SEQ      7
/*
 * 序号空间大小 = MAX_SEQ + 1 = 8，合法序号为 0~7。
 *
 * Go-Back-N 中发送窗口大小必须严格小于序号空间大小，
 * 否则当序号回绕时，旧帧和新帧会出现“编号重合但语义不同”的歧义。
 *
 * 这里窗口大小取 MAX_SEQ，也就是 7，满足：
 *     WINDOW_SIZE = 7 < 8 = MAX_SEQ + 1
 */
#define WINDOW_SIZE  MAX_SEQ
#define DATA_TIMER   2000

struct FRAME {
    unsigned char kind;             /* FRAME_DATA 或 FRAME_ACK */
    unsigned char ack;              /* 捎带确认号：最后一个按序正确收到的数据帧 */
    unsigned char seq;              /* 当前 DATA 帧序号 */
    unsigned char data[PKT_LEN];
    unsigned int  padding;          /* 给 CRC 预留写入空间，不直接参与帧字段设计 */
};

/*
 * 发送端窗口状态：
 *
 * ack_expected:
 *   发送窗口左边界，表示“最早一个尚未被确认的帧序号”。
 *   如果它是 3，说明 0/1/2 已经被累计确认，窗口当前从 3 开始。
 *
 * next_frame_to_send:
 *   发送窗口右边界的下一位置，表示“下一个新数据应该使用的序号”。
 *   注意它不是最后一个已发送帧，而是“下一次如果继续发，新帧该用哪个号”。
 *
 * nbuffered:
 *   当前窗口内已发送但尚未确认的帧数。
 *   因此发送窗口中未确认帧的逻辑区间可以理解为：
 *       [ack_expected, next_frame_to_send)
 *   区间长度就是 nbuffered，区间含义要结合循环序号理解。
 *
 * 例如：
 *   ack_expected = 3
 *   next_frame_to_send = 6
 *   nbuffered = 3
 * 表示序号 3、4、5 这三帧已经发出但还没确认。
 */
static unsigned char ack_expected = 0;        /* 发送窗口左边界：最早未确认帧 */
static unsigned char next_frame_to_send = 0;  /* 发送窗口右侧：下一个新帧可使用的序号 */
static unsigned char nbuffered = 0;           /* 当前发送窗口中已发但未确认的帧数 */

/*
 * 接收端状态：
 *
 * frame_expected:
 *   接收端当前期待收到的下一帧序号。
 *   只有 seq == frame_expected 的 DATA 帧才会被交付给网络层；
 *   所有乱序到达的帧都会被丢弃，然后回一个累计 ACK。
 *
 * 这正是 Go-Back-N 与选择重传的关键差别：
 *   Go-Back-N 不缓存乱序帧；
 *   选择重传会缓存乱序帧，等缺失帧补到后再按序交付。
 */
static unsigned char frame_expected = 0;      /* 接收端期望收到的下一帧序号 */

/*
 * 每个序号都对应一个发送缓存槽位。
 *
 * 为什么需要缓存？
 * 因为帧一旦超时，发送端必须能够“按原内容重传”。
 * 对 Go-Back-N 来说，超时后通常要把当前窗口内所有未确认帧都重发一遍，
 * 所以每个正在窗口中的序号都必须保留一份副本。
 */
static unsigned char out_buf[MAX_SEQ + 1][PKT_LEN]; /* 发送窗口缓存 */

/*
 * 物理层是否允许继续发送一帧。
 * 协议库以事件形式通知 PHYSICAL_LAYER_READY，本程序据此决定
 * 是否打开网络层、允许上层继续塞入新分组。
 */
static int phl_ready = 0;

static void inc(unsigned char *num)
{
    *num = (*num + 1) % (MAX_SEQ + 1);
}

/*
 * 判断 b 是否落在循环区间 [a, c) 内。
 *
 * 这里的“区间”不是普通整数区间，而是模 MAX_SEQ+1 的环形区间。
 * 例如序号空间是 0~7，那么：
 *   [3, 6) 表示 3,4,5
 *   [6, 2) 表示 6,7,0,1
 *
 * 这个函数主要用于判断收到的 ACK 是否仍然落在当前发送窗口中，
 * 从而决定发送窗口能否向前滑动。
 *
 * 例子 1：
 *   a=3, b=4, c=6  => true  （4 在 [3,6) 中）
 *
 * 例子 2：
 *   a=6, b=0, c=2  => true  （发生回绕，[6,2) 表示 6,7,0,1）
 *
 * 例子 3：
 *   a=6, b=3, c=2  => false （3 不在 [6,2) 中）
 */
static int between(unsigned char a, unsigned char b, unsigned char c)
{
    return ((a <= b) && (b < c)) ||
           ((c < a) && (a <= b)) ||
           ((b < c) && (c < a));
}

/*
 * 当前应发送的 ACK 号：确认“frame_expected 的前一帧”。
 *
 * 如果 frame_expected == 3，说明 0/1/2 已经按序收到了，
 * 此时应该回 ACK=2，告诉对方“截至 2 为止我都收到了”。
 *
 * 如果 frame_expected == 0，说明最近一个按序正确收到的帧其实是 7，
 * 所以这里需要用模运算处理序号回绕。
 */
static unsigned char latest_ack(void)
{
    return (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
}

/*
 * 给帧尾附加 CRC，然后交给协议库的 send_frame() 发到物理层。
 *
 * 发出后先把 phl_ready 置 0，表示当前物理层发送机会已经被占用，
 * 需要等待下一次 PHYSICAL_LAYER_READY 事件再继续放入新帧。
 */
static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

/*
 * 发送一个 DATA 帧。
 *
 * frame_nr 指定“发送窗口中的哪个序号槽位”要被发送。
 * 该槽位里的数据已经提前存放在 out_buf[frame_nr] 中。
 *
 * DATA 帧里捎带的 ack 是累计确认号：
 *   表示“截至 latest_ack() 为止的所有按序帧我都收到了”。
 *
 * 每发送一次 DATA 帧，都为该序号启动或重启定时器。
 * 这样一旦超时，就说明从该位置开始的累计确认迟迟未到。
 */
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

/*
 * 发送一个纯 ACK 帧。
 *
 * 当当前没有 DATA 可发送，或者即使有数据也希望立即反馈接收进度时，
 * 就用单独 ACK 通知对方“我现在已经按序收到哪里了”。
 *
 * 这份实现里没有使用延迟 ACK 定时器，而是收到 DATA 后立即回 ACK。
 */
static void send_ack_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_ACK;
    s.ack = latest_ack();

    dbg_frame("Send ACK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 2);
}

/*
 * Go-Back-N 的核心重传策略：
 *   只要当前窗口里任意一个未确认帧超时，
 *   就从发送窗口左边界 ack_expected 开始，
 *   把整个未确认窗口全部重发。
 *
 * 例子：
 *   未确认帧为 3,4,5
 *   如果 3 或 4 或 5 中任意一个超时，
 *   最终都要从 3 开始重传 3,4,5。
 *
 * 这与选择重传不同：
 *   选择重传通常只重发“具体超时的那一帧”。
 */
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
    lprintf("Go-Back-N sliding window, build: " __DATE__"  "__TIME__"\n");
    lprintf("MAX_SEQ=%d, WINDOW_SIZE=%d\n", MAX_SEQ, WINDOW_SIZE);

    /*
     * 启动时先关闭网络层。
     * 是否允许上层继续交付新分组，交给循环末尾统一根据
     * “窗口是否已满”以及“物理层是否可发”来判断。
     */
    disable_network_layer();

    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
            /*
             * 网络层已经准备好一个新的分组。
             *
             * 只要窗口未满，网络层就可以继续交付分组；
             * 新分组会被放入 next_frame_to_send 对应的缓存槽位，
             * 然后立即封装成 DATA 帧发送出去。
             *
             * 发送后：
             *   nbuffered           增加 1
             *   next_frame_to_send  前进到下一个可用序号
             *
             * 这体现了滑动窗口的“流水线发送”能力：
             * 在前面的帧还未确认时，后面的帧也可以继续进入链路。
             */
            get_packet(out_buf[next_frame_to_send]);
            nbuffered++;
            send_data_frame(next_frame_to_send);
            inc(&next_frame_to_send);
            break;

        case PHYSICAL_LAYER_READY:
            /*
             * 协议库通知：物理层当前可以接受下一帧发送。
             * 这里只更新状态，是否重新打开网络层放到循环末尾统一处理。
             */
            phl_ready = 1;
            break;

        case FRAME_RECEIVED:
            /*
            recv_frame 函数是一个物理层功能，用于接收帧数据。
            它接受一个指向缓冲区的指针 buf 和缓冲区大小 size，
            返回接收到的字节数。
            */
            len = recv_frame((unsigned char *)&f, sizeof f);
            /*
             * 最短合法 ACK 帧长度 = kind(1) + ack(1) + crc(4) = 6。
             * 这里沿用原实验代码的宽松判断 len < 5，同时用 CRC 作最终校验。
             * 一旦 CRC 错误，整帧直接丢弃，不做任何交付与确认推进。
             */
            if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                break;
            }

            if (f.kind == FRAME_ACK)
                dbg_frame("Recv ACK  %d\n", f.ack);

            if (f.kind == FRAME_DATA) {
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);

                /*
                 * Go-Back-N 接收端只接收按序到达的帧。
                 *
                 * 如果 f.seq == frame_expected：
                 *   说明这一帧正是接收端当前等待的下一帧，
                 *   可以安全交付给网络层，并把 frame_expected 往后推进。
                 *
                 * 如果 f.seq != frame_expected：
                 *   说明它是重复帧，或者是某个更靠后的乱序帧。
                 *   Go-Back-N 的接收端不会缓存它，而是直接丢弃。
                 *
                 * 无论当前 DATA 帧是否按序，都会回一个 ACK：
                 *   ACK 的含义始终是“截至 latest_ack() 为止我都按序收到了”。
                 * 这样发送方就能知道接收方目前的连续接收进度。
                 */
                if (f.seq == frame_expected) {
                    put_packet(f.data, len - 7);
                    inc(&frame_expected);
                }
                send_ack_frame();
            }

            /*
             * 处理累计确认。
             *
             * 对 Go-Back-N 来说，ACK 不是逐帧确认，而是累计确认。
             * 假设当前未确认帧是：
             *   ack_expected = 3
             *   next_frame_to_send = 6
             * 即窗口中未确认帧为 3,4,5
             *
             * 如果此时收到 ACK=4，表示对方已经按序收到 0~4，
             * 那么发送方就可以一次性确认并移出 3 和 4，
             * 于是窗口左边界从 3 连续滑动到 5。
             *
             * while 而不是 if 的原因就在这里：
             *   一个累计 ACK 可能同时确认多个仍在窗口中的帧。
             */
            while (nbuffered > 0 && between(ack_expected, f.ack, next_frame_to_send)) {
                nbuffered--;
                stop_timer(ack_expected);
                inc(&ack_expected);
            }
            break;

        case DATA_TIMEOUT:
            /*
             * Go-Back-N 的超时策略：任意未确认帧超时后，
             * 从最早未确认帧开始重传整个发送窗口。
             *
             * arg 是超时定时器对应的序号，但这里并不只重发 arg 对应的帧；
             * 只要窗口中有一帧超时，就统一回退到 ack_expected，
             * 把当前所有未确认帧按顺序重发。
             */
            dbg_event("---- DATA %d timeout\n", arg);
            resend_window();
            break;
        }

        /*
         * 统一决定是否重新打开网络层：
         *
         * 条件 1：nbuffered < WINDOW_SIZE
         *   发送窗口还没满，还能继续容纳新的未确认帧。
         *
         * 条件 2：phl_ready
         *   物理层当前允许再发送一帧。
         *
         * 两个条件必须同时满足，才能安全地让网络层继续向下交付新分组。
         * 否则就先关闭网络层，避免窗口溢出或物理层拥塞。
         */
        if (nbuffered < WINDOW_SIZE && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}

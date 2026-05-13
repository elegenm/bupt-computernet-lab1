#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/*
 * 选择重传 Selective Repeat 滑动窗口版本。
 *
 * 这份代码和 Go-Back-N 最本质的区别在于：
 * 1. 接收端允许缓存“窗口内乱序到达”的 DATA 帧；
 * 2. 发送端只重传“具体超时或被 NAK 指出的那一帧”。
 *
 * 因此，SR 不会像 GBN 那样因为中间某一帧出错，
 * 就把后续已经正确到达的帧全部白白浪费掉。
 *
 * 代价是：SR 需要维护更多状态，包括：
 * - 发送窗口中每一帧是否已被确认；
 * - 接收窗口中每一帧是否已到达；
 * - 接收缓存区里保存的乱序数据；
 * - 哪个缺失位置已经发送过 NAK，避免重复发送。
 */

#define MAX_SEQ      7
/*
 * 序号空间大小为 8，因此选择重传的发送/接收窗口最大只能取 4。
 *
 * 这是 SR 和 GBN 的一个重要差异：
 * - GBN 只要求发送窗口小于序号空间大小；
 * - SR 必须更严格，窗口大小不能超过序号空间的一半。
 *
 * 否则在序号回绕后，接收方无法区分：
 *   “这是旧窗口中重发的旧帧”
 * 还是
 *   “这是新窗口中第一次到达的新帧”。
 */
#define WINDOW_SIZE  ((MAX_SEQ + 1) / 2)
#define DATA_TIMER   2000

struct FRAME {
    unsigned char kind;     /* FRAME_DATA / FRAME_ACK / FRAME_NAK */
    unsigned char ack;      /* ACK/NAK 携带的序号 */
    unsigned char seq;      /* DATA 帧的发送序号 */
    unsigned char data[PKT_LEN];
    unsigned int  padding;  /* 给 CRC32 预留写入空间 */
};

/*
 * 发送端窗口状态：
 *
 * ack_expected:
 *   发送窗口左边界，也就是“最早一个尚未完全移出窗口的帧序号”。
 *
 * next_frame_to_send:
 *   下一个新分组应使用的发送序号。
 *
 * nbuffered:
 *   当前窗口中已发送但还没有彻底滑出窗口的帧数。
 *
 * out_buf:
 *   每个序号对应一份发送缓存。帧超时时必须能按原内容重传，
 *   因此只要帧还没离开窗口，就必须保留它的副本。
 *
 * acked:
 *   记录“某个序号是否已经收到单独确认”。
 *   这是 SR 相对 GBN 最典型的新增状态之一。
 *
 *   对 GBN 来说，ACK 更偏累计确认，窗口左边界之前的帧会成片确认；
 *   对 SR 来说，ACK 更像“逐帧确认”，因此发送端必须单独记住：
 *   3 收到了没有，4 收到了没有，5 收到了没有。
 */
static unsigned char ack_expected = 0;
static unsigned char next_frame_to_send = 0;
static unsigned char nbuffered = 0;
static unsigned char out_buf[MAX_SEQ + 1][PKT_LEN];
static unsigned char acked[MAX_SEQ + 1];

/*
 * 接收端窗口状态：
 *
 * frame_expected:
 *   接收窗口左边界，表示“当前最早还没按序交付给网络层的序号”。
 *
 * too_far:
 *   接收窗口右边界的下一位置，因此当前接收窗口是：
 *       [frame_expected, too_far)
 *
 * in_buf:
 *   接收缓存，用于保存窗口内乱序到达的帧内容。
 *   这是 SR 能优于 GBN 的关键：先存起来，等缺失帧补齐后再交付。
 *
 * arrived:
 *   记录接收窗口内某个序号是否已经到达。
 *   比如 arrived[3] = 1 表示“3 号帧已经在缓存里了”。
 *
 * no_nak:
 *   防止对同一个缺失位置连续反复发送 NAK。
 *   当发现 frame_expected 缺失时，发一次 NAK 就够了，
 *   等缺失帧真正补到后，再把 no_nak 重新置回允许发送状态。
 */
static unsigned char frame_expected = 0;
static unsigned char too_far = WINDOW_SIZE;
static unsigned char in_buf[MAX_SEQ + 1][PKT_LEN];
static unsigned char arrived[MAX_SEQ + 1];
static int no_nak = 1;

/*
 * 物理层发送机会是否可用。
 * 协议库会通过 PHYSICAL_LAYER_READY 事件通知当前可继续发送一帧。
 */
static int phl_ready = 0;

static void inc(unsigned char *num)
{
    *num = (*num + 1) % (MAX_SEQ + 1);
}

/*
 * 判断 b 是否落在循环区间 [a, c) 内。
 *
 * 这里的区间是“环形序号区间”，而不是普通整数区间。
 * 例如序号空间为 0~7 时：
 *   [2, 5) 表示 2,3,4
 *   [6, 2) 表示 6,7,0,1
 *
 * 它既用于发送窗口判断，也用于接收窗口判断。
 */
static int between(unsigned char a, unsigned char b, unsigned char c)
{
    return ((a <= b) && (b < c)) ||
           ((c < a) && (a <= b)) ||
           ((b < c) && (c < a));
}

/*
 * 当前序号是否位于接收窗口 [frame_expected, too_far) 内。
 *
 * 如果在窗口内，说明这帧虽然可能乱序，但仍然属于“可以接受并缓存”的范围。
 * 这和 GBN 不同：GBN 即使窗口内乱序，也通常直接丢弃。
 */
static int in_receive_window(unsigned char seq)
{
    return between(frame_expected, seq, too_far);
}

/*
 * 当前序号是否位于“上一轮接收窗口”中。
 * 这类帧通常是发送方在 ACK 丢失后重发的旧帧，接收端不再交付，
 * 但应重新回 ACK，避免发送方一直超时重传。
 */
static int in_previous_window(unsigned char seq)
{
    unsigned char low = (frame_expected + MAX_SEQ + 1 - WINDOW_SIZE) % (MAX_SEQ + 1);

    return between(low, seq, frame_expected);
}

/*
 * 给帧尾追加 CRC32 后发往物理层。
 *
 * 发出后将 phl_ready 置 0，表示当前物理层发送机会已使用，
 * 需要等待下一次 PHYSICAL_LAYER_READY 才能继续发送。
 */
static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

/*
 * 发送纯 ACK 帧。
 *
 * SR 中的 ACK 更偏逐帧确认语义：
 * ACK(nr) 的含义近似为“nr 号帧我已经收到了”，
 * 它不必像 GBN 那样主要依赖累计确认来推动窗口。
 */
static void send_ack_frame(unsigned char ack_nr)
{
    struct FRAME s;

    s.kind = FRAME_ACK;
    s.ack = ack_nr;

    dbg_frame("Send ACK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 2);
}

/*
 * 发送 NAK 帧。
 *
 * NAK(nr) 表示：
 *   “我现在缺少 nr 号帧，请尽快把它重传给我。”
 *
 * 发送后把 no_nak 置 0，避免在缺失帧还没补到之前，
 * 对同一个位置一遍又一遍重复发 NAK。
 */
static void send_nak_frame(unsigned char nak_nr)
{
    struct FRAME s;

    s.kind = FRAME_NAK;
    s.ack = nak_nr;
    no_nak = 0;

    dbg_frame("Send NAK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 2);
}

/*
 * 发送一个 DATA 帧。
 *
 * frame_nr 指明当前要发送哪个序号槽位中的数据。
 * 该数据已经提前存放在 out_buf[frame_nr] 中。
 *
 * 与 GBN 相比，这里的 ACK 字段不再承担“主要靠累计确认推动整窗滑动”的职责，
 * 更多只是顺便捎带当前接收方最近的接收进度。
 *
 * 每个已发送的 DATA 帧都拥有独立定时器，这是 SR 能够做到“单帧超时、单帧重传”
 * 的基础。
 */
static void send_data_frame(unsigned char frame_nr)
{
    struct FRAME s;

    s.kind = FRAME_DATA;
    s.seq = frame_nr;
    /*
     * 对 SR 来说，ACK 使用“逐帧确认”语义：
     * ACK 的 ack 字段仅确认当前接收方最近处理的某一个帧。
     * 当发送 DATA 时，这里顺便捎带确认“上一帧按序交付的序号”。
     */
    s.ack = (frame_expected + MAX_SEQ) % (MAX_SEQ + 1);
    memcpy(s.data, out_buf[frame_nr], PKT_LEN);

    dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

    put_frame((unsigned char *)&s, 3 + PKT_LEN);
    start_timer(frame_nr, DATA_TIMER);
}

/*
 * 标记某个具体序号已经被确认。
 *
 * 这是 SR 发送端的关键逻辑：
 * 1. 某个序号被 ACK 了，就把 acked[seq] 置 1；
 * 2. 但窗口左边界 ack_expected 不能随便跳过去，
 *    只有当“从左边界开始连续的一串帧都已确认”时，窗口才能真正向前滑。
 *
 * 例如：
 *   已发送未确认帧为 3,4,5
 *   先收到 ACK(5)
 * 则：
 *   acked[5] = 1
 * 但 ack_expected 仍然是 3，因为 3 和 4 还没确认。
 *
 * 直到后续再收到 ACK(3)、ACK(4)，窗口左边界才能连续向右滑动。
 */
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
    lprintf("Selective Repeat sliding window, build: " __DATE__"  "__TIME__"\n");
    lprintf("MAX_SEQ=%d, WINDOW_SIZE=%d\n", MAX_SEQ, WINDOW_SIZE);

    disable_network_layer();

    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
            /*
             * 窗口未满时，从网络层取一个新分组并放入相应的发送缓存。
             * SR 仍允许流水线发送，但每个帧都保留独立缓存与定时器。
             *
             * 与 GBN 不同的是，后续若某一帧出错，SR 不需要从窗口左边界整段回退，
             * 因此这些缓存槽位在语义上更像“可独立重传的帧副本”。
             */
            get_packet(out_buf[next_frame_to_send]);
            acked[next_frame_to_send] = 0;
            nbuffered++;
            send_data_frame(next_frame_to_send);
            inc(&next_frame_to_send);
            break;

        case PHYSICAL_LAYER_READY:
            phl_ready = 1;
            break;

        case FRAME_RECEIVED:
            len = recv_frame((unsigned char *)&f, sizeof f);
            if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                /*
                 * CRC 错误时无法知道究竟是哪一帧出错，只能提示当前
                 * 最早缺失的 frame_expected 重传。
                 *
                 * 因为接收端按序交付的最左边缺口就是 frame_expected，
                 * 所以从协议语义上，请求重传它最合理。
                 */
                if (no_nak)
                    send_nak_frame(frame_expected);
                break;
            }

            switch (f.kind) {
            case FRAME_ACK:
                dbg_frame("Recv ACK  %d\n", f.ack);
                /*
                 * 收到 ACK 后，仅标记这个具体序号已被确认，
                 * 然后再尝试从发送窗口左边界连续滑动。
                 */
                mark_acked(f.ack);
                break;

            case FRAME_NAK:
                dbg_frame("Recv NAK  %d\n", f.ack);
                /*
                 * NAK 指出的是“明确缺失”的那一帧，因此仅重传该帧。
                 * 这正是选择重传区别于整窗回退的核心特征。
                 */
                if (between(ack_expected, f.ack, next_frame_to_send) && !acked[f.ack])
                    send_data_frame(f.ack);
                break;

            case FRAME_DATA:
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);

                /*
                 * DATA 帧里捎带的 ACK 也要处理，它可能单独确认了某个发送帧。
                 * 因此收到一个 DATA 帧时，本机同时要做两件事：
                 * 1. 作为接收方，处理对方发来的 DATA；
                 * 2. 作为发送方，处理对方顺便带回来的 ACK。
                 */
                mark_acked(f.ack);

                if (in_receive_window(f.seq)) {
                    /*
                     * 如果收到的是窗口内帧：
                     *   1. 对乱序到达的情况，先缓存下来；
                     *   2. 立即回 ACK(f.seq)，表示该帧已收到；
                     *   3. 若窗口左边界已经连续可交付，则按序提交给网络层。
                     *
                     * 这就是 SR 的关键收益点：
                     *   先收、先存、等缺失帧补齐后再整体顺序交付。
                     */
                    if (f.seq != frame_expected && no_nak)
                        send_nak_frame(frame_expected);

                    if (!arrived[f.seq]) {
                        arrived[f.seq] = 1;
                        memcpy(in_buf[f.seq], f.data, len - 7);
                    }

                    /*
                     * 即使该帧不是当前最左边缺失帧，只要成功落入接收窗口并缓存，
                     * 也应立即回 ACK(f.seq)，告诉发送方“这帧你不用再发了”。
                     */
                    send_ack_frame(f.seq);

                    /*
                     * 只要接收窗口左边界 frame_expected 对应的帧已经到达，
                     * 就可以不断向网络层按序交付，并推进接收窗口。
                     *
                     * 例如：
                     *   先缓存了 3、4
                     *   后来 2 补到
                     * 则 while 会依次交付 2、3、4。
                     */
                    while (arrived[frame_expected]) {
                        put_packet(in_buf[frame_expected], len - 7);
                        arrived[frame_expected] = 0;
                        no_nak = 1;
                        inc(&frame_expected);
                        inc(&too_far);
                    }
                } else if (in_previous_window(f.seq)) {
                    /*
                     * 该帧已在过去某次按序交付过。
                     * 这种情况通常是 ACK 丢失，发送方重发了旧帧。
                     * 接收端不再重复交付，但要把 ACK 再发一遍。
                     */
                    send_ack_frame(f.seq);
                }
                break;
            }
            break;

        case DATA_TIMEOUT:
            dbg_event("---- DATA %d timeout\n", arg);
            /*
             * SR 超时时仅重传当前定时器对应的那一帧。
             *
             * 这和 GBN 的整窗回退正好构成鲜明对比：
             * GBN: 一帧超时，重传整窗
             * SR : 哪帧超时，重传哪帧
             */
            if (!acked[arg])
                send_data_frame((unsigned char)arg);
            break;
        }

        /*
         * 只有当：
         * 1. 发送窗口仍未满；
         * 2. 物理层当前允许再发一帧；
         * 才继续打开网络层，让上层交新的分组下来。
         */
        if (nbuffered < WINDOW_SIZE && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
    }
}

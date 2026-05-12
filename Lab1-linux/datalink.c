#include <stdio.h>
#include <string.h>

#include "protocol.h"
#include "datalink.h"

/* 数据帧重传超时时间，单位为毫秒。 */
#define DATA_TIMER  2000

/*
 * 当前示例程序使用停等协议。
 * DATA 帧包含 kind/ack/seq/data/CRC，ACK 帧只使用 kind/ack/CRC。
 * CRC 字段不在结构体里直接填写，而是在 put_frame() 发送前追加。
 */
struct FRAME { 
    unsigned char kind; /* 帧类型：FRAME_DATA 或 FRAME_ACK */
    unsigned char ack;
    unsigned char seq;
    unsigned char data[PKT_LEN]; 
    unsigned int  padding;
};

/*
 * 停等协议的发送端状态：
 *   frame_nr  - 下一帧 DATA 的序号，只会在 0 和 1 之间切换
 *   buffer    - 发送缓存，停等协议只需要缓存一个分组
 *   nbuffered - 已发送但未确认的分组数，本程序中最大为 1
 *
 * 接收端状态：
 *   frame_expected - 当前期望收到的对方 DATA 帧序号
 *
 * 物理层状态：
 *   phl_ready - 物理层是否可以继续发送一帧
 */
static unsigned char frame_nr = 0, buffer[PKT_LEN], nbuffered;
static unsigned char frame_expected = 0;
static int phl_ready = 0;

/* 给帧尾部追加 CRC32 校验码，然后交给物理层发送。 */
static void put_frame(unsigned char *frame, int len)
{
    *(unsigned int *)(frame + len) = crc32(frame, len);
    send_frame(frame, len + 4);
    phl_ready = 0;
}

/* 把当前缓存的分组封装为 DATA 帧发送，并启动该序号的重传定时器。 */
static void send_data_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_DATA;
    s.seq = frame_nr;
    /* 在 DATA 帧中捎带确认号，确认已经正确收到的上一帧。 */
    s.ack = 1 - frame_expected;
    memcpy(s.data, buffer, PKT_LEN);

    dbg_frame("Send DATA %d %d, ID %d\n", s.seq, s.ack, *(short *)s.data);

    put_frame((unsigned char *)&s, 3 + PKT_LEN);
    start_timer(frame_nr, DATA_TIMER);
}

/* 没有 DATA 帧可捎带确认时，单独发送 ACK 帧。 */
static void send_ack_frame(void)
{
    struct FRAME s;

    s.kind = FRAME_ACK;
    s.ack = 1 - frame_expected;

    dbg_frame("Send ACK  %d\n", s.ack);

    put_frame((unsigned char *)&s, 2);
}

int main(int argc, char **argv)
{
    int event, arg;
    struct FRAME f;
    int len = 0;

    protocol_init(argc, argv); 
    lprintf("Designed by Jiang Yanjun, build: " __DATE__"  "__TIME__"\n");

    disable_network_layer();

    /*
     * 主事件循环。协议库会通知网络层就绪、物理层就绪、
     * 收到帧、数据帧定时器超时等事件。
     */
    for (;;) {
        event = wait_for_event(&arg);

        switch (event) {
        case NETWORK_LAYER_READY:
            /* 网络层有一个分组可取；停等协议一次只接收一个分组进入缓存。 */
            get_packet(buffer);
            nbuffered++;
            send_data_frame();
            break;

        case PHYSICAL_LAYER_READY:
            /* 物理层已经可以继续接收一个待发送帧。 */
            phl_ready = 1;
            break;

        case FRAME_RECEIVED: 
            len = recv_frame((unsigned char *)&f, sizeof f);
            /* 丢弃长度异常或 CRC 校验失败的帧。 */
            if (len < 5 || crc32((unsigned char *)&f, len) != 0) {
                dbg_event("**** Receiver Error, Bad CRC Checksum\n");
                break;
            }
            if (f.kind == FRAME_ACK) 
                dbg_frame("Recv ACK  %d\n", f.ack);
            if (f.kind == FRAME_DATA) {
                dbg_frame("Recv DATA %d %d, ID %d\n", f.seq, f.ack, *(short *)f.data);
                /*
                 * 只向网络层交付期望序号的 DATA 帧。
                 * 如果收到重复帧，仍然回 ACK，但不会重复交付数据。
                 */
                if (f.seq == frame_expected) {
                    put_packet(f.data, len - 7);
                    frame_expected = 1 - frame_expected;
                }
                send_ack_frame();
            } 
            /* ACK 确认了当前未确认的 DATA 帧，发送端序号前进一步。 */
            if (f.ack == frame_nr) {
                stop_timer(frame_nr);
                nbuffered--;
                frame_nr = 1 - frame_nr;
            }
            break; 

        case DATA_TIMEOUT:
            /* 超时时还没等到 ACK，重传当前缓存的 DATA 帧。 */
            dbg_event("---- DATA %d timeout\n", arg); 
            send_data_frame();
            break;
        }

        /*
         * 只有没有未确认 DATA 帧，并且物理层可发送时，才允许网络层继续给包。
         * 这是停等协议“一次只在路上放一帧”的核心限制。
         */
        if (nbuffered < 1 && phl_ready)
            enable_network_layer();
        else
            disable_network_layer();
   }
}

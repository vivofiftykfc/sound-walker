
#define _GNU_SOURCE

#include "tts.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <errno.h>
#include <sys/select.h>


#define TTS_STATUS_TIMEOUT_MS  80    /* 状态查询超时 (ms) — 模块在线时 10ms 内响应 */
#define TTS_ACK_TIMEOUT_MS    200    /* ACK 等待超时 (ms) */
#define TTS_BUSY_MAX_WAIT_MS  800   /* 模块忙碌时最长等待 (ms) */
#define TTS_POLL_INTERVAL     60    /* 轮询间隔 (ms) */
#define TTS_POWERON_DELAY_MS  300   /* 模块上电稳定时间 (ms) */


#define FRAME_HEAD   0xFD
#define CMD_TEXT     0x01   /* 文本播报命令 */
#define CMD_STATUS   0x21   /* 状态查询 */
#define ENCODE_UTF8  0x04
#define ACK_SUCCESS  0x41
#define STATUS_IDLE  0x4F
#define STATUS_BUSY  0x4E

struct tts_t {
    int  fd;               /* 串口文件描述符 */
    char device[64];       /* 设备路径 (仅用于日志) */
};

/* ═══════════════════════════════════════
 * Internal helpers
 * ═══════════════════════════════════════ */

/** 读取一字节, timeout_ms 内无数据返回 -1. */
static int read_byte(int fd, int timeout_ms)
{
    fd_set rfds;
    struct timeval tv;

    FD_ZERO(&rfds);
    FD_SET(fd, &rfds);
    tv.tv_sec  = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;

    int r = select(fd + 1, &rfds, NULL, NULL, &tv);
    if (r <= 0) return -1;

    uint8_t b = 0;
    if (read(fd, &b, 1) != 1) return -1;
    return b;
}

/**
 * 发送帧并等待 ACK. 返回 0 成功, -1 失败.
 * 内部不打印错误信息 — 由调用方决定是否报告.
 */
static int send_frame(tts_t* tts, const uint8_t* frame, int len)
{
    tcflush(tts->fd, TCIOFLUSH);

    if (write(tts->fd, frame, len) != len) return -1;
    tcdrain(tts->fd);

    int ack = read_byte(tts->fd, TTS_ACK_TIMEOUT_MS);
    return (ack == ACK_SUCCESS) ? 0 : -1;
}

/**
 * 快速状态查询. 返回 1=空闲, 0=忙碌, -1=无响应.
 * 超时短 (80ms), 适合在循环中调用.
 */
static int check_idle(tts_t* tts)
{
    uint8_t cmd[4] = { FRAME_HEAD, 0x00, 0x01, CMD_STATUS };

    tcflush(tts->fd, TCIOFLUSH);
    write(tts->fd, cmd, 4);
    tcdrain(tts->fd);

    int st = read_byte(tts->fd, TTS_STATUS_TIMEOUT_MS);
    if (st == STATUS_IDLE) return 1;
    if (st == STATUS_BUSY) return 0;
    return -1;
}

/* ═══════════════════════════════════════
 * Public API
 * ═══════════════════════════════════════ */

tts_t* tts_init(const char* device, int baudrate)
{
    if (!device) device = "/dev/ttyAMA0";
    if (baudrate <= 0) baudrate = 115200;

    tts_t* tts = calloc(1, sizeof(tts_t));
    if (!tts) return NULL;
    strncpy(tts->device, device, sizeof(tts->device) - 1);

    
    tts->fd = open(device, O_RDWR | O_NOCTTY);
    if (tts->fd < 0) {
        free(tts);
        return NULL;  /* 无串口设备, 静默返回 */
    }

    
    struct termios tty;
    memset(&tty, 0, sizeof(tty));
    if (tcgetattr(tts->fd, &tty) != 0) {
        close(tts->fd);
        free(tts);
        return NULL;
    }

    speed_t speed;
    switch (baudrate) {
        case 9600:     speed = B9600;     break;
        case 19200:    speed = B19200;    break;
        case 38400:    speed = B38400;    break;
        case 57600:    speed = B57600;    break;
        case 115200:   speed = B115200;   break;
        case 230400:   speed = B230400;   break;
        default:       speed = B115200;   break;
    }
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag &= ~PARENB;
    tty.c_cflag &= ~CSTOPB;
    tty.c_cflag &= ~CSIZE;
    tty.c_cflag |=  CS8;
    tty.c_cflag &= ~CRTSCTS;
    tty.c_cflag |= (CLOCAL | CREAD);

    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY);
    tty.c_iflag &= ~(INLCR | ICRNL | IGNCR);
    tty.c_oflag &= ~OPOST;

    tty.c_cc[VMIN]  = 0;
    tty.c_cc[VTIME] = 1;   /* 100ms read timeout */

    if (tcsetattr(tts->fd, TCSANOW, &tty) != 0) {
        close(tts->fd);
        free(tts);
        return NULL;
    }

    usleep(TTS_POWERON_DELAY_MS * 1000);

    
    if (check_idle(tts) != 1) {
        close(tts->fd);
        free(tts);
        return NULL;  /* 模块不在线, 静默返回 */
    }

    
    tts_speak(tts, "[m0]");
    usleep(50000);
    tts_speak(tts, "[v2]");
    usleep(50000);
    tts_speak(tts, "[s4]");
    usleep(50000);
    tts_speak(tts, "[t6]");
    usleep(100000);

    return tts;
}

void tts_destroy(tts_t* tts)
{
    if (!tts) return;
    if (tts->fd >= 0) {
        tcflush(tts->fd, TCIOFLUSH);
        close(tts->fd);
    }
    free(tts);
}

int tts_speak(tts_t* tts, const char* text)
{
    if (!tts || tts->fd < 0 || !text || !*text) return -1;

    
    int waited = 0;
    while (waited < TTS_BUSY_MAX_WAIT_MS) {
        int st = check_idle(tts);
        if (st == 1) break;     /* 空闲, 可以发 */
        if (st == -1) break;    /* 无响应也继续 (试一次) */
        /* st == 0: 忙碌, 继续等 */
        usleep(TTS_POLL_INTERVAL * 1000);
        waited += TTS_POLL_INTERVAL;
    }

    
    int text_len = strlen(text);
    int data_len = 2 + text_len;  /* cmd + encode + text */
    uint8_t frame[512];

    if (5 + text_len > (int)sizeof(frame)) return -1;

    frame[0] = FRAME_HEAD;
    frame[1] = (data_len >> 8) & 0xFF;
    frame[2] =  data_len & 0xFF;
    frame[3] = CMD_TEXT;
    frame[4] = ENCODE_UTF8;
    memcpy(frame + 5, text, text_len);

    return send_frame(tts, frame, 5 + text_len);
}

int tts_wait_idle(tts_t* tts, int timeout_ms)
{
    if (!tts || tts->fd < 0) return 0;

    int elapsed = 0;
    while (timeout_ms < 0 || elapsed < timeout_ms) {
        int st = check_idle(tts);
        if (st == 1) return 0;
        if (elapsed >= timeout_ms) break;
        usleep(TTS_POLL_INTERVAL * 1000);
        elapsed += TTS_POLL_INTERVAL;
    }
    return -1;
}

/**
 * tts.h — SNR9816TTS 语音合成模块驱动
 *
 * 通信: UART 115200 8N1
 * 协议: 0xFD + len(2B) + cmd + encode + text
 *
 * 接线 (树莓派 5):
 *   TTS TX → RPi RX (GPIO15, pin10)
 *   TTS RX ← RPi TX (GPIO14, pin8)
 *   VCC    → 5V (pin2/4)
 *   GND    → GND (pin6/9/14)
 */

#ifndef TTS_H
#define TTS_H

#include <stdint.h>

typedef struct tts_t tts_t;

/**
 * 初始化 SNR9816TTS 模块
 * @param device   串口设备路径 (如 "/dev/serial0")
 * @param baudrate 波特率 (通常 115200)
 * @return tts 句柄, 失败返回 NULL
 */
tts_t* tts_init(const char* device, int baudrate);

/**
 * 关闭 TTS 模块, 释放资源
 */
void tts_destroy(tts_t* tts);

/**
 * 合成并播报 UTF-8 文本 (阻塞直到模块空闲, 然后异步播放)
 * @param tts  tts 句柄
 * @param text UTF-8 编码文本
 * @return 0 成功, -1 失败
 */
int tts_speak(tts_t* tts, const char* text);

/**
 * 等待 TTS 播报完毕
 * @param tts         tts 句柄
 * @param timeout_ms  最大等待毫秒 (-1 = 无限等待)
 * @return 0 空闲, -1 超时
 */
int tts_wait_idle(tts_t* tts, int timeout_ms);

#endif /* TTS_H */

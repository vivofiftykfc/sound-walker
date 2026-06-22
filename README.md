# 声纹锁 — 嵌入式离线声纹识别系统

ARM 嵌入式 Linux 平台实现离线声纹注册与识别。两条技术路线在同一硬件平台上实现并对比。

演示地址：[【【大作业】声纹锁：MFCC+DTW vs CAM++神经网络 演示】 ](https://www.bilibili.com/video/BV1HJ7P6oE8r/?share_source=copy_web&vd_source=a46bd1f34ae458cc54764fa7c96aeaa4)

## 路线对比

| 维度 | v1: MFCC + DTW | v2: sherpa-onnx + CAM++ |
|------|---------------|------------------------|
| 特征 | 13维 MFCC | 512维神经网络嵌入 |
| 匹配 | 帧序列DTW余弦距离 | 向量空间余弦相似度 |
| 同人 | d=0.15~0.28 ✅ | cos=0.80~0.95 ✅ |
| 变声 | d=0.31~0.38 ⚠️ 模糊带 | cos=0.30~0.40 ✅ 清晰拒绝 |
| 延迟 | ~200ms | ~500ms |
| 依赖 | 纯C零依赖 | sherpa-onnx + ONNX模型 |

## 硬件

- 麦克风：C-Media PCM2902 (USB, 44100Hz)
- TTS：SNR9816TTS (UART 115200)
- 存储：SQLite 3

## 编译

v1 (MFCC+DTW)：
```
cd embed_v1 && make -f Makefile.rpi && ./voiceprint_lock
```

v2 (sherpa-onnx)：
```
pip install sherpa-onnx
cd embed && make -f Makefile.rpi && ./voiceprint_lock
```

## 项目结构

```
├── embed/          v2 (sherpa-onnx CAM++)
├── embed_v1/       v1 (MFCC + DTW)
├── tts/            TTS语音播报驱动
├── scripts/        实验脚本 / PPT生成
└── docs/           调研报告 / 实验报告 / PPT
```

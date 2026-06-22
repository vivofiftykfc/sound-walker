# v2 — sherpa-onnx + CAM++ 声纹识别

树莓派5上运行的离线声纹锁 v2 版本，基于 sherpa-onnx 和 CAM++ 神经网络。

## 编译

```
pip install sherpa-onnx
make -f Makefile.rpi
./voiceprint_lock
```

需要提前下载 ONNX 模型文件到 `models/` 目录。

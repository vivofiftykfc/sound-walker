# v1 — MFCC + DTW 声纹识别

树莓派5上运行的离线声纹锁 v1 版本，纯 C 实现，无外部依赖。

## 编译

```
make -f Makefile.rpi
./voiceprint_lock
```

依赖：`libasound2-dev`、`libsqlite3-dev`

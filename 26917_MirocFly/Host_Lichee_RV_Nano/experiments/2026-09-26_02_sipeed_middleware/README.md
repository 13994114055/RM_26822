# 2026-09-26_02_sipeed_middleware（实验，已弃用）

**目的**：用我们自写的 `sophgo_middleware.c`（NV21 版）取帧。
**结果**：❌ 全黑。链接的是 **sipeed/官方中间件**，与板上 scpcom 中间件版本不匹配（见 `../../difficulty_and_method.md` B7）。
**结论**：弃用，转入 `../2026-09-26_03_scpcom_formA/`（Form A）。
**保留原因**：作为“版本混用”反例与排查过程存档。

文件：`camera.c` / `sophgo_middleware.c` / `sophgo_middleware.h` / `Makefile`（旧，指向不存在的 `LicheeRV-Nano-Build/`，仅存档）。

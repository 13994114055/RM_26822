# BETAFPVG473-z-branch-mspsignfix（2026-10-17 深夜）

**内容**：Z 分支固件（测距做高度参考） + **MSP2 光流遥测符号修复**。

| 项 | 值 |
|---|---|
| 版本串 | `INAV/BETAFPVG473 10.0.0 Oct 17 2026 / 19:02:04 (7e2d65a5)` |
| hex sha256 | `7c0d30613812bd57b4ed97b48a1571309fd7a2d0eb24e72e3ee83be180cf9c58` |
| FLASH1 | 458394B / 480KiB（93.26%） |
| 源码改动 | `src/main/fc/fc_msp.c:778-781` 四行：`sbufWriteU16(dst, RADIANS_TO_DEGREES(...))` → `(int16_t)lrintf(RADIANS_TO_DEGREES(...))`（见 `mspsignfix-fc_msp.diff`，相对 inav-g4dbg 树） |
| 修复问题 | float→uint16 隐式转换 **负值饱和为 0** → MSP2 光流遥测"永不负值"（全向动作只见单符号；详见下位机 AGENT_GUIDE §6-24） |
| 回退固件 | `firmware/BETAFPVG473-z-branch-e112ce42/`（旧在飞版，刷前备份） |
| 配置恢复 | `firmware/INAV_10.0.0_cli_20261017_full-diffall.txt`（含 `align_opflow=CW90`、`nav_mc_hover_thr=1585`、`inav_max_surface_altitude=400` 等） |

> ⚠️ **版本串溯源注意**：`(7e2d65a5)` = **构建时仓库 HEAD**（构建目录在 git 仓库内，`git describe` 取的是外层仓库 HEAD；`inav-g4alt/` 是 gitignore 目录，固件源码改动不反映在版本串里）。
> **固件溯源以 hex sha256 + 构建时间为准。**

**刷写**（USB DFU，.hex 保留配置区）：

```bash
dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D inav_10.0.0_BETAFPVG473.hex
```

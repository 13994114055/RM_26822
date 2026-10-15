# BETAFPV G473 INAV 10.0.0 — surface-as-Z branch（MirocFly 定高/定点前置）

> 2026-10-15 构建。**这是给"无气压计 / 无 GPS"的 G473 加"测距（激光）作为高度 Z 参考"的固件**，
> 目的：让 INAV 的 `ALTHOLD`/`POSHOLD` 能在本板上准入（默认它只认气压计/GPS 的高度源）。

## 相对基线（在飞版）改了什么

- 基线 = `../BETAFPVG473-motor-af-i2c-fix/`（2026-09-26 构建，电机 AF + I2C 修复）。
- 本次在**新工作树 `drone/inav-g4alt/`** 实现（3 文件，改动细节见 `../../AGENT_GUIDE.md` §5.7）：
  - `navigation_pos_estimator.c`：`estimationCalculateCorrection_Z` 新增“测距 Z 分支”
    （气压计与 GPS-Z 均无效 + 测距有效时启用；零点解锁前跟随、解锁后冻结；Z 无效时直接吸附初始化；
    P/V 修正复用 `w_z_surface_p/v` 并按 reliability/bell 曲线加权；`epv` 用测距残差更新）。
  - `navigation_pos_estimator_agl.c`：记录 `surface.updateDt`。
  - `navigation_pos_estimator_private.h`：surface 结构加 `updateDt`；新增 `INAV_SURFACE_DEFAULT_EPV 50.0f`。
- **未动**气压计/GPS 分支；无气压计时，该分支成为唯一 Z 源。

## 产物与校验

| 文件 | 大小 | sha256 |
|---|---|---|
| `...surface-z-branch.hex` | 1,291,713 | `049bbc94a2f486ad735988ae571a99920bc95c3d8f4a78189d88e140946d26f0` |
| `...surface-z-branch.bin` | 491,178 | `9be4ca0d4ac292cdb8783128f69bf3aa349768d47d67d96fc8a02998676a5b1f` |
| （elf，未归档，路径）`drone/inav-g4alt/build/bin/BETAFPVG473.elf` | 8.4M | `c996d3e1c02f2da3ac838318892b067bd34ad8b28508d394994ea9e3b415279f` |

- 编译：i5 `arm-none-eabi-gcc 16.2.0` + `cmake -B build -G Ninja -DCOMPILER_VERSION_CHECK=OFF` + `ninja -C build BETAFPVG473`
- 体积：`FLASH1 458410B / 480KiB = 93.26%`、`RAM 88756B`、`CCM 11872B`
- 配置区（`0x08004000–0x08007FFF`）在 hex 中**无记录** → **刷 .hex 保留 FC 配置**。

## 刷写（⚠️ 拔桨）

按住 BOOT 插 USB 进 DFU（`0483:df11`）后：

```bash
cd /home/user/code/26822_RM/26917_MirocFly/Low_MCU/new_mcu/drone
docker run --rm --privileged -v /dev/bus/usb:/dev/bus/usb --network host \
  -v "$PWD/firmware/BETAFPVG473-surface-z-branch":/fw \
  tpuc_mlir:latest bash -lc \
  'apt-get update -qq && apt-get install -y -qq dfu-util && \
   dfu-util -d 0483:df11 -a 0 -s 0x08000000:leave -D /fw/BETAFPVG473-inav-10.0.0-surface-z-branch.hex'
```

## 刷后验证

- `python3 connection/fccli.py --cmd version --cmd status`
  - `version` 应为 **`Oct 15 2026 / 12:20:15`**（旧版 = `Sep 26 2026 / 18:30:49`）
  - `status`：`GYRO/ACC=BMI270 OK`、`RANGEFINDER=OK、OPFLOW=OK`、`BARO/GPS=NONE`

## 回退（若不可用）

刷回在飞版（.hex，同样保留配置）：
`../BETAFPVG473-motor-af-i2c-fix/BETAFPVG473-inav-10.0.0-motor-af-i2c-fix.hex`
配置若丢 → 回粘 `../INAV_10.0.0_cli_20261015_full-diffall.txt` + `save`。

## 行为变化（台架重点观察）

- 无气压计/GPS 时估计器改用测距做 Z：**Z 有效后 `ALTHOLD` 可准入**（台架用 `DEBUG_ALTITUDE` / 黑色盒子观察）。
- `estAglStatus`：`EST_NONE` → `EST_USABLE`（Z 由无效变有效所致；消费者多按 `==EST_TRUSTED` 门控，本机不触发）。
- 多旋翼降落检测：`safetyTime` 5000→1000ms、垂速判定改用真实值 → **涉及 LAND 失效/自动上锁，重点观察**。
- `MSP_SONAR_ALTITUDE` 不变（仍返回测距原始值）→ 上位机 `alt=` 不受影响。

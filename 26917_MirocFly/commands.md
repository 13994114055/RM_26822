# MirocFly 常用命令速查（给用户）

> 目的：**基础命令自己查**（板子在不在线、怎么跑某功能…）。不够细就对了——详细版看：
> `Host_Lichee_RV_Nano/diary.md`（上位机操作手册）、`tools/README.md`（工具）、
> `Low_MCU/new_mcu/connection/README.md`（对接/FC 配置）。

**先设一个别名（省去长参数，后续板子命令都用它）**
```bash
bsh(){ ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no root@10.222.2.1 "$@"; }
```

## 1. 板子在不在线
```bash
ip -br addr | grep 10.222          # 有 enp0s20*(RNDIS) 才可能有网
ping -c2 10.222.2.1                # 不通就再试 10.222.1.1（板地址会变）
bsh 'uptime'                       # 能登进去；若 up 很小=在反复重启→供电/USB 问题
# USB 掉线排查(i5)： journalctl -k | grep -iE "359f|error -71|disconnect"
#   多为 Hub/线/供电 → 去 Hub、直插主机后置口、短线、稳 5V
# 换镜像/换板后若报 host key 变了： ssh-keygen -R 10.222.2.1
```

## 2. 板子常用信息
```bash
bsh 'cd /root/mirocfly && ls'                                  # 已部署的程序
bsh 'cat /sys/kernel/debug/ion/cvi_carveout_heap_dump/summary' # ION 占用(used 应=0)
bsh 'reboot'                                                   # 干净重启(ION 泄漏后)
```

## 3. 交叉编译 + 部署（在 i5）
```bash
cd Host_Lichee_RV_Nano/experiments/<实验目录> && make
scp <程序> root@10.222.2.1:/root/mirocfly/
```

## 4. 运行 / 停止（铁律）
```bash
# 运行必须带库路径；板上没有 timeout → 后台+sleep+killall；停止用 killall(SIGTERM)，别 kill -9
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./<程序> <参数> >/tmp/x.log 2>&1 &); sleep 12; killall <程序>; tail -20 /tmp/x.log'
bsh 'killall <程序>'     # 停止（相机类程序同一时刻只能跑一个！）
```

## 5. 相机 / 视觉类（详见 diary.md §2）
```bash
# 取帧
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./frame_grab 1280 720 1 10 5 >/tmp/fg.log 2>&1 &); sleep 12; killall frame_grab'
# RTSP（必须显式绑 IP）
bsh 'cd /root/mirocfly && (RTSP_IP=10.222.2.1 LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./rtsp_grab 1280 720 1 >/tmp/rtsp.log 2>&1 &)'
ffplay -rtsp_transport tcp rtsp://10.222.2.1:8554/live ;  bsh 'killall rtsp_grab'
# HSV 绿检测
bsh 'cd /root/mirocfly && (LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd ./green_detect 1280 720 60 >/tmp/gd.log 2>&1 &); sleep 16; killall green_detect; grep -aE "FOUND|no green" /tmp/gd.log'
```

## 6. 接管 / 飞行类（实验18/19/20；需 FC，串口 `/dev/ttyS0` @230400）
```bash
./paradigm mon  /dev/ttyS0 230400                          # 只读: q/alt/ch5-8/ovr
./paradigm dry  1280 720 0 /dev/ttyS0 230400               # 算值但不发 RC(安全)
./paradigm run  1280 720 0 /dev/ttyS0 230400               # 真机(经 MSP RC Override 接管三轴)
./run_paradigm.sh 120 0.4 150 60                           # 预启动(脱离电脑, 到点自停)
./stop_paradigm.sh                                         # 停
./flow_damp mon  /dev/ttyS0 230400                         # 只读光流/测距
./flow_damp damp /dev/ttyS0 230400                         # 光流水平阻尼
./takeover dry  1280 720 0                                 # (实验18)仅相机/PTY
```

## 7. 链路验证（板上）
```bash
bsh 'cd /root/mirocfly && ./msp_probe /dev/ttyS0 230400'   # 期望 … LINK OK
```

## 8. FC 配置（i5，USB VCP；docker 免 sudo）
```bash
# ⚠️ 用完必须 exit/save，并等 ~15–20s，否则 FC 的 UART2 MSP 不响应（板子链路会"NO RESPONSE"）
cd Low_MCU/new_mcu && docker run --rm --device=/dev/ttyACM0 -v "$PWD":/mcu -w /mcu/connection tpuc_mlir:latest \
  python3 fccli.py --cmd version --cmd status
# 改配置：  … python3 fccli.py --cmd "set xxx = yyy" --cmd save
# 打开 Configurator 会独占 /dev/ttyACM0 → 先关（ killall inav-configurator ）
```

## 9. 黑匣子解码（i5，`tools/blackbox_decode.py`）
```bash
python3 tools/blackbox_decode.py ~/Downloads/blackbox_log_*.TXT --list
python3 tools/blackbox_decode.py <log> --slow                              # 接管/模式/ESC
python3 tools/blackbox_decode.py <log> --index N --tmin 9 --npoints 60 \
  --trace "rcData[3],rcCommand[1],attitude[1],surfaceRaw,motor[0],motor[1],motor[2],motor[3]"
```

## 10. 关键坑（一句话记）
- 运行都要 `LD_LIBRARY_PATH=/mnt/system/usr/lib:/mnt/system/usr/lib/3rd`。
- 相机类程序**互斥**；停止用 `killall`(SIGTERM)，**别 `kill -9`**（ION 泄漏）。
- 板子/FC 改完配置 → **等 15–20s**（FC 会重启、UART2 MSP 会短暂断）。
- 板子 USB 极易掉：**去 Hub、直插后置口、短线、稳 5V**（详见 `difficulty_and_method.md` B18）。
- 开关映射（实验20）：`ch5`=解锁；`ch6>1300 或 ch8>1300`=接管；`ch8>900`=ANGLE；`ch6`=范式B；`ch7`=定点/定高。

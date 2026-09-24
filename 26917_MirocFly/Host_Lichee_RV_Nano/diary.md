# 📄 今晚 LicheeRV Nano 操作总结（文档草稿）

## 一、准备工作：烧录系统镜像到 SD 卡

从 GitHub Releases 下载预编译镜像 2026-01-14-16-03-d4003f.tar.xz。

解压得到 .img 文件：
bash

xz -d 2026-01-14-16-03-d4003f.tar.xz
tar -xf 2026-01-14-16-03-d4003f.tar

确认 SD 卡设备名（例如 /dev/sdb）：bash
lsblk

写入镜像（注意不要加 conv=sync，避免卡死）：bash
sudo dd if=2026-01-14-16-03-d4003f.img of=/dev/sdb bs=1M status=progress
sync

写入完成后，SD 卡应出现两个分区：

- sdb1：16M（启动分区）
- sdb2：1.6G（根文件系统）

## 二、硬件连接与上电

将烧录好的 SD 卡插入 LicheeRV Nano。

用 USB 线连接板子与电脑（USB-C 口）。

板子上电启动。

## 三、电脑端识别 USB 网络

查看内核日志，确认板子被识别为 RNDIS/CDC NCM 网卡：bash
sudo dmesg | tail -40

应看到 rndis_host、cdc_ncm、enp0s20f0u3、enp0s20f0u3i2 等字样。

查看网络接口：bash
ip link show

新增两个接口：enp0s20f0u3 和 enp0s20f0u3i2。

## 四、通过 DHCP 自动获取 IP

安装 dhcpcd（如果尚未安装）：bash
sudo pacman -S dhcpcd

对两个接口分别获取 IP：bash
sudo dhcpcd enp0s20f0u3
sudo dhcpcd enp0s20f0u3i2

获取结果：

- enp0s20f0u3 获得 10.222.2.100/24，网关 10.222.2.1
- enp0s20f0u3i2 获得 10.222.1.100/24，网关 10.222.1.1

## 五、验证连通性并 SSH 登录

ping 网关：bash
ping -c 3 10.222.2.1
ping -c 3 10.222.1.1

SSH 登录板子：bash
ssh root@10.222.2.1

首次连接需确认指纹，输入 yes。

输入密码（默认可能是 root、空密码、liceepi 或 sipeed），成功后看到 # 提示符。

## 六、退出

在板子终端输入：
bash
exit

或按 Ctrl+D。

## 📌 关键命令速查

| 操作 | 命令 |
|------|------|
| 查看 SD 卡 | lsblk |
| 写入镜像 | sudo dd if=镜像.img of=/dev/sdX bs=1M status=progress |
| 查看 USB 日志 | sudo dmesg | tail -40 |
| 查看网卡 | ip link show |
| 自动获取 IP | sudo dhcpcd 接口名 |
| 查看 IP | ip addr show 接口名 |
| 测试连通 | ping -c 3 网关IP |
| SSH 登录 | ssh root@10.222.2.1 |
| 退出 SSH | exit 或 Ctrl+D |

## 今晚的成果

成功烧录系统、启动板子、配置 USB 网络、通过 SSH 登录 Linux 系统。

下次可以继续在板子上编译程序、使用外设，或者配置交叉编译环境。
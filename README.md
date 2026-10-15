# RM_26822
RM之旅（机器人/嵌入式工程总仓库）

## 目录导航

| 目录 / 文件 | 是什么 | 入口 |
|---|---|---|
| `26917_MirocFly/` | **自主撞击无人机**（上位机 LicheeRV Nano + 下位机 BetaFPV G473/INAV） | `26917_MirocFly/README.md`、`26917_MirocFly/agent.md` |
| `vision/` | **视觉学习资料**（PDF）：《了解CV和RoboMaster视觉组——你的最后一本计算机视觉入门手册》**352 页**（湖南大学跃鹿战队，CC-BY，作者 NeoZng） | 直接打开 `vision/release1.2了解CV和RoboMaster视觉组.pdf` |
| `Rule/` | **规则与资料**：RM 2027 规则手册/参赛手册/制作规范（PDF）+ 面试评分表（docx） | — |
| `sdcard_images/` | **系统镜像**（SD 卡镜像，`*.img` + `*.img.xz`） | — |
| `LoopMaster/` | 非侵入式 MCU 变量示波器（SWD/CMSIS-DAP 探针读 RAM 变量 + 波形/结构体展开） | `LoopMaster/README.md` |
| `SOLDIER_LEG_4/` | RM 足式机器人（Keil MDK，STM32F407）工程 | `SOLDIER_LEG_4/agents.md` |
| `test_c620_motor/`、`test_C620_Motor__2nd__CMake/` | STM32H723 + C620 电机测试工程 | 各自 README/源码 |
| `26911_自制cnc电池架/` | CNC 电池架 CAD（SolidWorks/STEP） | — |
| `agent问答示例.md` | ⭐ **给 AI 的行为要求**（教学优先：讲清"做什么/为什么/谁做基础"，基础代码可代写但知识必须教会；不一口气全代劳） | AI 开工前必读 |
| `AI教学协作指南.md` | ⭐ **本指南的总结版**（教学优先/我主导/你提问/红线），**可复制进任何新工程根目录**当第一准则 | 复制到新工程即用 |
| `26917_MirocFly.tar` | 整仓库打包备份（~56GB，不入库） | — |

> 无人机飞行日志（黑匣子）：`26917_MirocFly/Low_MCU/new_mcu/drone/logs/`（用 `26917_MirocFly/tools/blackbox_decode.py` 解码）。

## 给 AI Agent 的阅读顺序

1. **行为方式**：`agent问答示例.md`（原始示例对话）→ `AI教学协作指南.md`（总结版，贯穿始终的回答思路）；
2. **无人机项目**：`26917_MirocFly/agent.md`（全局上下文/决策）→ `26917_MirocFly/README.md`（总览）→ 各 `AGENT_GUIDE.md`（上位机/下位机开发指导）；
3. **视觉知识**：`vision/` 里的 PDF（352 页，CV 基础 → RM 视觉组实践）。

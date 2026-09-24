# AGENTS.md

This file is a project orientation note for future coding agents working in this workspace.

## Project Overview

This is a Keil MDK STM32F407 embedded project for an RM-style legged robot. The workspace contains two separate firmware projects:

- `Chassis/`: lower board / chassis firmware. Handles leg-wheel chassis control, balance control, referee system, super capacitor, chassis motors, and lower-board safety state.
- `Gimbal/`: upper board / gimbal firmware. Handles yaw/pitch gimbal control, shooting mechanism, remote control input, USB vision auto-aim, and communication with the chassis board.

Keil project files:

- `Chassis/MDK-ARM/Cboard.uvprojx`
- `Gimbal/MDK-ARM/Cboard.uvprojx`

The MCU target is STM32F407IGHx. The project uses STM32 HAL, FreeRTOS CMSIS-OS v1 style APIs, CAN, UART DMA, USB CDC, BMI088 IMU, and multiple motor types including DJI 3508/6020 and DM8009.

## Startup Flow

Both `Chassis` and `Gimbal` follow the same broad startup pattern in their `Src/main.c` files:

1. `HAL_Init()`
2. `SystemClock_Config()` configures the STM32F407 to 168 MHz using HSE + PLL.
3. CubeMX-generated peripheral init:
   - GPIO
   - DMA
   - I2C
   - SPI
   - TIM
   - CAN1/CAN2
   - USART
   - CRC
   - USB device
4. User init:
   - `DWT_Init(168)`
   - `BMI088_init(&hspi1, 1)`
   - buzzer timer/PWM start and `Buzzer_Off()`
   - `CAN_Init()`
5. `MX_FREERTOS_Init()` creates RTOS tasks.
6. `osKernelStart()` starts the scheduler.

After the scheduler starts, normal code runs inside FreeRTOS tasks; the infinite loop in `main()` should never run.

## FreeRTOS Task Layout

### Chassis firmware

Defined in `Chassis/Src/freertos.c`.

- `Check_Task`, priority `osPriorityBelowNormal`, stack 256, period 10 ms
  - Initializes USB device and check subsystem.
  - Runs online checks, control state selection, buzzer error indication, VOFA debug transmission.
- `Ins_Task`, priority `osPriorityRealtime`, stack 1024, period 1 ms
  - Reads BMI088 and updates attitude/acceleration through quaternion EKF.
- `Chassis_Task`, priority `osPriorityHigh`, stack 1024, period 1 ms
  - Main leg-wheel chassis control chain.
- `Board_Can_Task`, priority `osPriorityNormal`, stack 256, period 3 ms
  - Sends lower-board status to upper board.
- `Ref_Task`, priority `osPriorityNormal`, stack 256, period 3 ms
  - Parses referee system data and updates UI drawings.
- `Super_Cap_Task`, priority `osPriorityAboveNormal`, stack 256, period 5 ms
  - Controls super capacitor power input based on referee power limits and buffer energy.

### Gimbal firmware

Defined in `Gimbal/Src/freertos.c`.

- `Check_Task`, priority `osPriorityBelowNormal`, stack 256, period 10 ms
  - Initializes USB device and check subsystem.
  - Runs online checks, state selection, buzzer/debug logic.
- `Ins_Task`, priority `osPriorityRealtime`, stack 1024, period 1 ms
  - Reads BMI088 and updates attitude/acceleration.
- `Gimbal_Task`, priority `osPriorityHigh`, stack 512, period 1 ms
  - Main gimbal and shooting control chain.
- `Board_Can_Task`, priority `osPriorityNormal`, stack 256, period 4 ms
  - Sends remote-control and gimbal status data to chassis.
- `Aim_Task`, priority `osPriorityRealtime`, stack 1024, period 1 ms
  - Sends USB CDC packets to the vision host and receives auto-aim data.
- `Limit_Task`, priority `osPriorityHigh`, stack 512, period 1 ms
  - Computes heat/bullet limit state for shooting control.

## Ready Flag Synchronization

Many tasks have a `xxx_ready_flag` global. Their init functions set the flag and then wait for `all_ready_flag`.

The `Check_Init()` task waits for the other task-ready flags, delays about 2 seconds, runs the ready buzzer, and then sets `all_ready_flag = 1`. This releases the other tasks from their init wait loops.

Important files:

- `Chassis/Task/Src/Check_Task.c`
- `Gimbal/Task/Src/Check_Task.c`

## Chassis Control Chain

Main file: `Chassis/Task/Src/Chassis_Task.c`.

`Chassis_Init()` initializes:

- velocity Kalman/filter state via `V_Kf_Init(&V_kf)`
- leg length PID: `Leg_L_Pid[2]`
- leg pole angle PID: `Leg_P_Pid[2]`
- roll compensation PID: `Roll_Pid`
- gravity/velocity/acceleration feedforward: `G_Comp[2]`
- compensation parameters in `Compensation_Amount`

`Chassis_Task()` runs roughly once per millisecond and calls this pipeline:

1. `Variable_Information_Acquisition()`
   - Converts motor feedback and IMU values into body yaw, roll/theta, leg angles, leg lengths, wheel speeds, estimated forward speed, estimated yaw speed, and estimated body height.
2. `Slip_Check_Calc()`
   - Detects wheel slip using acceleration and wheel-speed changes.
3. `All_Theta_Err_Check()`
   - Detects fall state and abnormal leg angles.
4. `Chassis_Control()`
   - Selects target speed, yaw, leg length, and behavior based on control state and remote input.
5. `Fast_Processing()`
   - Handles fast state/target processing.
6. `Check_Stuck_Leg()`
   - Detects stuck leg conditions.
7. `YAW_Parameter_Processing()`
   - Processes chassis yaw parameters.
8. `Bump_Control()`
   - Obstacle/step behavior.
9. `Jump_Up()`
   - Jump/off-ground behavior.
10. `Leg_Control()`
   - Computes leg control outputs.
11. `LQR()`
   - Main balance/state feedback controller.
12. `Vmc()`
   - Virtual model control; converts leg force/torque demand into joint motor torque/current targets.
13. `Chassis_Can_Data_Send()`
   - Sends motor commands over CAN.

The chassis firmware is easiest to understand as:

sensor/motor feedback -> state estimation -> mode/target generation -> LQR/VMC control -> CAN motor output.

## Gimbal Control Chain

Main file: `Gimbal/Task/Src/Gimbal_Task.c`.

`Gimbal_Init()` initializes:

- normal yaw/pitch position and speed PID loops
- auto-aim yaw/pitch PID loops
- absolute yaw PID
- friction wheel RPM/torque PID loops
- trigger/feed motor position and speed PID loops
- yaw and friction-wheel feedforward
- shooting state, initially `Close`

`Gimbal_Task()` runs roughly once per millisecond and calls:

1. `Variable_Information_Acquisition()`
   - Reads friction wheel RPM, trigger motor RPM/current/virtual position, yaw/pitch motor and IMU state.
2. `Gimbal_Control()`
   - Handles RC/mouse/keyboard mode logic, shooting permissions, and gimbal reference values.
3. `Auto_Aim()`
   - Applies vision target data from `aim_rx`.
4. `Gimbal_Target_Limit()`
   - Limits yaw/pitch targets.
5. `Shoot_Control()`
   - Controls friction wheel and trigger/feed behavior.
6. `Gimbal_Controllor()`
   - Runs PID controllers and computes motor outputs.
7. `Gimbal_Can_Data_Send()`
   - Sends gimbal/shooting motor commands over CAN.

Important user-control behavior in `Gimbal_Task.c`:

- RC mode:
  - right switch controls shooting system open/close.
  - selected channel values enable firing and auto aim.
- PC/mouse mode:
  - left mouse: fire permission.
  - right mouse: auto aim permission.
  - `Q`: toggles friction wheel state.
  - `B`: toggles single-shot flag.
  - `C`: toggles chassis-break/off flag.

## Inter-Board CAN Communication

The upper board and lower board exchange status through CAN1.

### Gimbal -> Chassis

Sent in `Gimbal/Task/Src/Board_Can_Task.c`; received in `Chassis/APP/Src/Can_Feedback.c`.

- CAN ID `0x080`: remote-control forwarding
  - `rc.ch[1]`
  - `rc.ch[4]`
  - keyboard bitmask `key.v`
  - switch states `rc.s[0]`, `rc.s[1]`
- CAN ID `0x160`: upper-board/gimbal status
  - `Link_Sit.err_num`
  - yaw 6020 encoder value
  - fire permission
  - single-shot flag
  - friction/shoot condition
  - chassis-break flag
  - `rc.ch[0]`
- CAN ID `0x120`: VT03 data
  - `VT03.key`
  - `VT03.mode_sw`

### Chassis -> Gimbal

Sent in `Chassis/Task/Src/Board_Can_Task.c`; received in `Gimbal/APP/Src/Can_Feedback.c`.

- CAN ID `0x104`: lower-board/chassis status
  - fall flag
  - enemy color
  - heat limit
  - current barrel heat
  - cooling value
  - initial bullet speed
  - chassis yaw rate/down dyaw

## Motor Feedback CAN IDs

### Chassis motor feedback

Handled in `Chassis/APP/Src/Can_Feedback.c`.

CAN1 FIFO0:

- `0x10`: DM8009 joint motor 0
- `0x20`: DM8009 joint motor 1
- `0x080`, `0x120`, `0x160`: upper-board messages

CAN2 FIFO1:

- `0x30`: DM8009 joint motor 2
- `0x40`: DM8009 joint motor 3
- `0x201`: DJI 3508 wheel motor 1
- `0x202`: DJI 3508 wheel motor 0
- `0x610`, `0x612`: super capacitor/power module status

### Gimbal motor feedback

Handled in `Gimbal/APP/Src/Can_Feedback.c`.

CAN2 FIFO1:

- `0x205`: yaw DJI 6020
- `0x207`: pitch DJI 6020
- `0x201`: trigger/feed DJI 3508
- `0x202`: friction wheel DJI 3508
- `0x203`: friction wheel DJI 3508

## Remote Control Input

Remote control parsing exists in both projects:

- `Chassis/APP/Src/Remote_Control.c`
- `Gimbal/APP/Src/Remote_Control.c`

The code uses USART3 + DMA double buffering for SBUS/DBUS style frames.

Important flow:

1. `Remote_Control_Init()` calls `RC_Init()` with two DMA receive buffers.
2. `SBUS_TO_RC()` detects UART IDLE and switches DMA buffer.
3. `sbus_to_rc()` parses 18-byte frames into:
   - `rc.ch[0..4]`
   - `rc.s[0..1]`
   - mouse x/y/z
   - mouse left/right buttons
   - keyboard bitmask `key.v`
4. Channels are offset by `RC_CH_VALUE_OFFSET`.

The gimbal board appears to be the primary remote receiver. It forwards key chassis-relevant remote values to the chassis board over CAN ID `0x080`.

## INS / IMU Attitude Estimation

Main file: `Task/Src/Ins_Task.c` in each firmware tree.

The INS task:

- reads BMI088 accel/gyro data with `BMI088_Read(&BMI088)`
- applies installation parameter correction through `IMU_Param_Correction()`
- runs quaternion EKF via `IMU_QuaternionEKF_Update()`
- updates:
  - `INS.q`
  - `INS.Yaw`
  - `INS.Pitch`
  - `INS.Roll`
  - `INS.YawTotalAngle`
  - `INS.MotionAccel_b`
  - `INS.MotionAccel_n`
- runs IMU temperature control periodically

Many control modules depend on the global `INS` object.

## Vision Auto-Aim

Main file: `Gimbal/Task/Src/Aim_Task.c`.

`Aim_Task()` calls `Send_Packet()` every loop.

`Send_Packet()` sends a USB CDC packet to the vision host containing:

- packet header `FH_TX`
- enemy color
- quaternion `INS.q`
- yaw/pitch angles and velocities
- bullet count
- bullet speed
- CRC16 checksum

`Recieve_Host(uint8_t *buff)` checks frame header `FH_RX`, verifies CRC16, and copies valid data into global `aim_rx`.

`Gimbal_Task()` later consumes `aim_rx` in `Auto_Aim(&aim_rx, &Gimbal_Status)`.

## Referee System and UI

Main file: `Chassis/Task/Src/Ref_Task.c`.

`Ref_Init()`:

- initializes referee data structures
- initializes FIFO
- initializes referee UART DMA buffers

`Ref_Task()`:

- calls `Referee_UnpackFifoData()` to parse referee protocol frames
- periodically pushes UI elements such as fire state, gyro state, auto-aim state, fall state, single-shot state, crosshair, direction arc, and super capacitor display

The referee data is used by chassis power limiting, heat limiting, UI, and state display.

## Super Capacitor Control

Main file: `Chassis/Task/Src/Super_Cap_Task.c`.

`Super_Cap_Control()`:

- uses `Power_Heat_Data.buffer_energy` and `Robot_Status.chassis_power_limit`
- PID-adjusts requested input power to protect buffer energy
- writes `Input_Power`
- calls `Pm_Power_Set(&hcan2, Input_Power * 100, 0x00)`
- calls `Can_Power_Read(&hcan2)`

## Safety and State Selection

Important file: `Chassis/Task/Src/Check_Task.c`.

`Check_Control()` sets `Controlled_State` based on link errors, upper-board errors, and remote mode.

Known states include:

- `ERO`: error/protection state
- `STOP`: stopped state
- `MOUSE`: keyboard/mouse mode
- `RC`: remote-control mode

The chassis detects remote/vision/motor/gimbal link status, fall state, off-ground state, abnormal leg angles, stuck leg state, and power/heat constraints.

## Suggested Reading Order

For future Q&A, read in this order unless the user asks about a specific file:

1. `Chassis/Src/freertos.c` and `Gimbal/Src/freertos.c`
   - Understand task scheduling and periods.
2. `Chassis/Task/Src/Chassis_Task.c`
   - Understand lower-board main control logic.
3. `Gimbal/Task/Src/Gimbal_Task.c`
   - Understand upper-board gimbal/shooting logic.
4. `Chassis/APP/Src/Can_Feedback.c` and `Gimbal/APP/Src/Can_Feedback.c`
   - Understand where motor and board-to-board data enters the system.
5. `Chassis/Task/Src/Check_Task.c` and `Gimbal/Task/Src/Check_Task.c`
   - Understand safety state and link checks.
6. `Task/Src/Ins_Task.c` in either firmware tree
   - Understand IMU and attitude source.
7. `Gimbal/Task/Src/Aim_Task.c`
   - Understand USB auto-aim protocol.
8. `Chassis/Task/Src/Ref_Task.c`
   - Understand referee protocol and UI.

## Notes for Future Agents

- This is not a git repository in the current workspace.
- Prefer reading the `Task/` and `APP/` directories before generated HAL or middleware code.
- The Chinese comments in several files may appear mojibake depending on file encoding; preserve existing encoding/style when editing.
- Do not refactor generated CubeMX sections casually. User code is usually inside `USER CODE` blocks or project-specific `APP/` and `Task/` files.
- The project has duplicated structure under `Chassis/` and `Gimbal/`; check both sides before assuming a symbol is unique.
- Many globals are shared across modules. Before changing a struct/global, search all references in both firmware trees.
- This code controls physical hardware. Be conservative with control gains, CAN IDs, task periods, and safety-state behavior.

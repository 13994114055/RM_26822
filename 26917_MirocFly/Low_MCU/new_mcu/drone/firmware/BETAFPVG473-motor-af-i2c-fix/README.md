# BETAFPV G473 INAV 10.0.0 motor AF and I2C audit fix

This build includes the final audit fixes before flashing.

## Fixes

- Motor 4: `PC13/TIM8_CH4N` AF corrected from `D(5, 8)` (AF5) to
  `D(6, 8)` (AF6). Betaflight 4.5.1 uses `timer C13 AF6`.
- Beeper PWM path: `PA8/TIM1_CH1` corrected from AF1 to AF6.
- I2C: `bus_i2c_hal.c` had no STM32G4 hardware-map branch, leaving the I2C
  device map empty. STM32G4 now uses the F7-style hardware map with G4 RCC and
  PCLK setup, enabling I2C1 on PA15/PB7.

All timer pins used by this board were checked against Betaflight:

- PB0 / TIM3_CH3
- PB1 / TIM3_CH4
- PB2 / TIM5_CH1
- PB6 / TIM8_CH1
- PA8 / TIM1_CH1
- PC13 / TIM8_CH4N

## Output files

- `BETAFPVG473-inav-10.0.0-motor-af-i2c-fix.hex`
- `BETAFPVG473-inav-10.0.0-motor-af-i2c-fix.bin`
- `BETAFPVG473-inav-10.0.0-motor-af-i2c-fix.elf`

Flash address: `0x08000000`.

## Size

```text
FLASH1: 458682 B / 480 KiB   93.32%
RAM:     88744 B / 108 KiB   80.24%
CCM:     11872 B /  20 KiB   57.97%
```

## SHA-256

```text
D74C3EEA7EBF46D9CA73EAD27390546B37BC775B0B8D968BC085A332E4331E9B  HEX
0D55E4352D107D47BE2E979D20655AFDEFA9EDF33262B4CD8A7841AD9B597701  BIN
0B75A242938DECB81CEE420861E7C2604B0926BE9E75EB66C83344F48DBC4249  ELF
```

Remove propellers before flashing or motor testing.
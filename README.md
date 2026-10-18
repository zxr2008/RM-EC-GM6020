# GM6020 正弦跟随 + 看门狗保护

RoboMaster 电控第一次考核作业。用 STM32F407（C 型开发板）通过 CAN 控制 GM6020 电机，
分别跟随以正弦波变化的速度曲线与位置曲线，并使用独立看门狗（IWDG）做保护。

## 功能

| 模式 | 说明 | 切换方式 |
|---|---|---|
| 速度跟随 | 转速跟随 0.2 Hz 正弦，幅值 ±1.5 rad/s | `kMode = 0` |
| 位置跟随 | 角度跟随 0.2 Hz 正弦，幅值 ±0.6 rad | `kMode = 1` |

保护机制：

- **掉线保护**：连续 100 ms 收不到电机反馈 → 输出归零并锁存（需断电重启）
- **超速保护**：转速反馈超过 35 rad/s → 同上
- **过温保护**：电机温度超过 70 ℃ → 同上
- **独立看门狗**：IWDG 0.8 s，喂狗语句放在控制函数最后一行，程序卡死则自动复位

## 硬件

- RoboMaster C 型开发板（STM32F407IGHx）
- GM6020 直流无刷电机 ×1
- 24 V 电源
- DAP-Link / ST-Link 调试器

## 接线

| 开发板 | 电机 |
|---|---|
| CAN_H（PD0 / CAN1_RX） | CAN_H |
| CAN_L（PD1 / CAN1_TX） | CAN_L |
| GND | 与电源共地 |

> 上电顺序：先接电机电源与 CAN，再给控制板上电。否则上电瞬间收不到反馈，
> 100 ms 后就会触发掉线保护并锁死。

## 编译

需要：

- CMake ≥ 3.22
- Ninja
- GNU Arm Toolchain ≥ 11（本项目用 13.3）

```bat
build.bat
```

产物在 `build/` 下：`C_project.elf` / `.hex` / `.bin`。

## 烧录

```bat
flash_cboard.bat
```

## 代码结构

```
Tasks/
├── inc/
│   ├── ControlTask.h     任务接口（三个函数）
│   ├── Gm6020.h          电机类：CAN 帧编解码
│   └── Pid.h             PID 控制器
└── src/
    ├── ControlTask.cpp   ★ 作业本体：1 kHz 控制循环
    ├── Gm6020.cpp        电机驱动（队伍提供）
    └── Pid.cpp           PID 实现（队伍提供）
```

控制流程（每 1 ms 执行一次）：

```
正弦参考 → 位置环(可选) → 速度环 → 电压限幅 → CAN 下发
              ↑                                    │
              └──────── 电机反馈（0x205）───────────┘
```

## 可调参数

全部集中在 `Tasks/src/ControlTask.cpp` 顶部的参数区：

| 参数 | 默认值 | 含义 |
|---|---|---|
| `kMotorId` | 1 | 电机拨码 ID，必须与实物一致 |
| `kMode` | 0 | 0=速度跟随，1=位置跟随 |
| `kFreqHz` | 0.2 | 正弦频率（Hz） |
| `kVelAmp` | 1.5 | 速度幅值（rad/s） |
| `kPosAmp` | 0.6 | 位置幅值（rad） |
| `kVoltMax` | 2000 | 电压给定限幅（满量程 25000 ≈ 24 V） |

## 分工说明

| 内容 | 来源 |
|---|---|
| `C_project.ioc`、`C_board_src/`、`Pid`、`Gm6020` | 队伍提供的骨架与驱动 |
| `Tasks/src/ControlTask.cpp` | 本人编写 |
| `CMakeLists.txt`、`build.bat`、`flash_cboard.bat` | 本人在队伍模板基础上配置 |

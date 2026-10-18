# STM32CubeMX 中文操作手册（照着点就行）

> 目标：把 `D:\第一部分考核\C_project.ioc` 变成一份能编译的工程。
> 全程你只需要点鼠标，不用读英文句子，按下面的"看到 → 点哪"走。
> 版本：CubeMX 6.18.1。不同小版本按钮位置可能差一点点，认**名字**就行。

---

## 一、先认清界面五个区域

打开 CubeMX 后，窗口长这样（不用记英文，记位置）：

```
┌──────────────────────────────────────────── ① GENERATE CODE ┐
│ File  Window  Help                                            │
├───────────────────────────────────────────────────────────────┤
│ [Pinout & Configuration] [Clock Configuration] [Project Manager]│ ← ⑤ 底部标签
├──────────────┬─────────────────────────┬──────────────────────┤
│ ② 左侧分类    │  ③ 中间：芯片引脚图      │ ④ 右侧：配置面板      │
│ System Core  │      ┌─────────┐        │  （点左边某项才出现） │
│ Connectivity │      │ 芯片方块 │        │                      │
│ Analog       │      └─────────┘        │                      │
│ Timers       │                         │                      │
│ Middleware   │                         │                      │
└──────────────┴─────────────────────────┴──────────────────────┘
```

| 区域 | 中文意思 | 你什么时候用它 |
|---|---|---|
| ① 右上角大按钮 | **生成代码**（GENERATE CODE） | 最后一步按它 |
| ② 左侧分类栏 | 外设分类：内核/通信/模拟/定时器 | 检查 CAN、定时器、看门狗 |
| ③ 中间芯片图 | 引脚分配图，绿点=已用 | 看 PD0/PD1 有没有标成 CAN |
| ④ 右侧配置面板 | 选中某项后，这里出参数 | 改波特率、分频值 |
| ⑤ 底部三个标签 | 引脚配置 / 时钟树 / 工程设置 | 前两个看，第三个填 |

---

## 二、必认的英文词（只有这 16 个）

### 弹窗里的（只会问一次）

| 英文 | 中文 | 你点哪个 |
|---|---|---|
| `Migrate` | 迁移到新版本 | **点它**（我们要生成代码） |
| `Continue` | 继续（不改版本） | 不用点 |
| `Cancel` | 取消 | 不用点 |
| `OK` / `Yes` / `No` | 确定 / 是 / 否 | 一般点 OK |

### 顶部菜单

| 英文 | 中文 |
|---|---|
| `File` | 文件 |
| `Load Project` | **加载工程**（就是打开 .ioc 文件） |
| `Save Project` | 保存工程 |
| `Window` / `Help` | 窗口 / 帮助（不用管） |

### 底部三个标签

| 英文 | 中文 | 用途 |
|---|---|---|
| `Pinout & Configuration` | 引脚与外设配置 | 看外设开没开 |
| `Clock Configuration` | 时钟树 | 看主频是不是 168 MHz |
| `Project Manager` | **工程管理器** | 填名字、选工具链、生成 |

### 左侧分类栏

| 英文 | 中文 |
|---|---|
| `System Core` | 系统内核（RCC 时钟源、SYS 调试口） |
| `Connectivity` | 通信（**CAN1** 在这里） |
| `Analog` | 模拟（**IWDG 独立看门狗**在这里） |
| `Timers` | 定时器（**TIM6** 在这里） |
| `Middleware` | 中间件（本项目用不到） |

### 生成设置里的

| 英文 | 中文 | 该填什么 |
|---|---|---|
| `Project Name` | 工程名 | `C_project` |
| `Project Location` | 工程存放路径 | `D:\第一部分考核` |
| `Toolchain / IDE` | 工具链 | 下拉选 **`CMake`** |
| `Generate Under Root` | 在根目录下生成 | **必须打勾** |
| `Code Generator` | 代码生成选项（标签页） | 进去确认一项 |
| `Keep User Code when re-generating` | 重新生成时保留你写的代码 | 保持 **Yes**（默认就是） |
| `GENERATE CODE` | 生成代码 | 右上角大按钮 |

---

## 三、主流程：七步

### 第 0 步 · 打开软件

开始菜单搜 `STM32CubeMX`，双击。第一次会跳过一堆欢迎页，看到主界面就行。

### 第 1 步 · 加载工程

菜单 `File` → `Load Project...` → 弹文件框，进 `D:\第一部分考核` → 选中 `C_project.ioc` → 打开。

> 也可以直接把 `C_project.ioc` 拖进 CubeMX 窗口。

### 第 2 步 · 处理版本弹窗（关键）

会弹一个框，大致写着：

```
This project was created with STM32CubeMX V6.15.0 ...
Do you want to migrate ... ?
        [ Cancel ]  [ Continue ]  [ Migrate ]
```

**点 `Migrate`。** 意思是"把工程升级到 6.18 格式"，只有升级后才能用新版生成代码。

> 弹窗里如果出现 `backup` 或 `Create backup` 字样，勾上，它会自动备份原 .ioc，更安全。

### 第 3 步 · 确认界面是"引脚视图"

看底部标签，**`Pinout & Configuration`** 应该是选中状态（底色不同）。
中间应该出现一块芯片方块，边上一堆引脚名。

### 第 4 步 · 检查 4 个地方（只看，不要改）

| 检查什么 | 怎么找 | 应该看到 |
|---|---|---|
| CAN 引脚 | 中间芯片图找 `PD0`、`PD1` | `PD0` 旁写 `CAN1_RX`，`PD1` 旁写 `CAN1_TX`（绿色） |
| CAN 速率 | 左侧 `Connectivity` → 点 `CAN1` → 右侧 `Parameter Settings` | `Baud Rate` = `1000000` |
| CAN 中断 | 还是 CAN1 页面，点 `NVIC Settings` 小标签 | `CAN1 RX0 interrupt` 前面**打勾** |
| 定时器 | 左侧 `Timers` → 点 `TIM6` → 右侧 | `Prescaler` = `84-1`，`Counter Period` = `1000-1` |
| 看门狗 | 左侧 `Analog` → 点 `IWDG` | `Activated` 打勾，`Prescaler`=`64`，`Reload`=`400` |

> **这五项目前都已经配好了**，你只是确认没被迁移弄丢。如果哪项不一样，先别改，把屏幕截图发我。

### 第 5 步 · 填工程设置

点底部标签 **`Project Manager`**。左边会出现一列子项（`Project` / `Code Generator` / `Advanced Settings`...），默认在 `Project`。

在右边填：

| 栏位 | 填什么 |
|---|---|
| `Project Name` | `C_project` |
| `Project Location` | `D:\第一部分考核`（注意：CubeMX 会在这里**再套一层同名文件夹**，见下方注意） |
| `Application Structure` | 保持默认 `Advanced` |
| `Toolchain / IDE` | 下拉框选 `CMake` |
| `Generate Under Root` | **打勾**（这一项最关键） |

> **注意路径**：CubeMX 的规则是"Location + Project Name"拼成最终目录。如果下面预览显示成 `D:\第一部分考核\C_project\`，那就重复嵌套了。正确做法：把 `Project Location` 改成 `D:\`，让最终路径显示为 `D:\第一部分考核\`。
> **判断标准：填完后看一眼它显示的最终路径，必须是 `D:\第一部分考核\` 结尾，不能是 `D:\第一部分考核\C_project\`。**

然后点左边 `Code Generator`，确认：

- `STM32Cube Firmware Library Package` 是 `Copy only the necessary library files`（默认）
- `Keep User Code when re-generating` 打勾（默认）

### 第 6 步 · 生成

点**右上角** `GENERATE CODE` 按钮。

- 如果按钮是灰色不能点：说明 `Project Location` 为空或路径非法，回去补。
- 第一次生成会问是否下载固件包：**不会**，因为 FW_F4 V1.28.3 你本地已有。如果真弹了，点 `Cancel`，把截图发我。

等待 10~40 秒，结束时弹一个信息框，写着 `Code generation finished` 之类，点 `OK` / `Close`。

### 第 7 步 · 生成完必做两件事

**① 把 CMakeLists 换回队伍模板**（CubeMX 会覆盖它）

打开 `D:\第一部分考核`，把 `CMakeLists.template.txt` **复制**一份，改名成 `CMakeLists.txt`，覆盖掉现有那个。

**② 验证生成了什么**

`D:\第一部分考核` 里应该新出现：

```
Core\        ← main.c 等
Drivers\     ← HAL 库
cmake\       ← gcc-arm-none-eabi.cmake（之前编译报错缺的就是它）
```

然后双击 `D:\第一部分考核\build.bat`，等它跑完，看有没有出现 `C_project.elf`。

---

## 四、判断成功的标志

| 阶段 | 成功表现 |
|---|---|
| 生成代码 | `D:\第一部分考核\Core\`、`Drivers\`、`cmake\` 三个文件夹出现 |
| 编译 | 双击 build.bat 后出现 `build\C_project.elf`（还有 .hex、.bin） |
| 失败常见提示 | `cmake/gcc-arm-none-eabi.cmake 找不到` = 代码没生成成功；`arm-none-eabi-gcc 找不到` = PATH 问题（build.bat 已处理） |

---

## 五、SYS 里两个容易选错的选项（必看）

左侧 `System Core` → 点 `SYS`，右侧只需确认两项：

| 项 | 必须是什么 | 选错的后果 |
|---|---|---|
| `Debug` | **`Serial Wire`** | 选 `JTAG` 会多占 PA13/PA14/PA15/PB3/PB4 五个脚，你的 SPI1 就用不了；选 `No Debug` 会把 SWD 两个脚变成普通 IO，**之后调试器再也连不上**（得按住复位用 connect-under-reset 才能救回来） |
| `Timebase Source` | **`SysTick`**（默认） | 不要选 TIM6！TIM6 已经被 1 kHz 控制环占用了，两者会打架 |

**你这个工程已经是 `Serial Wire`**：`.ioc` 里 PA13 = `SYS_JTMS-SWDIO`、PA14 = `SYS_JTCK-SWCLK`，而且 PB3/PB4 还被 SPI1 占着（JTAG 开着的话这两个脚是拿不到的）。所以这两项都不用改。

> 为什么是 Serial Wire：你手上的 DAP-LINK 只接了 `SWD`（SWDIO）和 `SCK`（SWCLK）两根信号线，这正是 SWD 两线模式。JTAG 是五线，你用不上。

---

## 六、卡住了怎么办

只要出现下面任一情况，**先截图，别乱点**：

1. 弹窗里有 `Error`、`Cannot`、`not found` 字样
2. `GENERATE CODE` 按钮是灰的
3. 生成后 `Core\` 文件夹没出现
4. 检查第 4 步时发现某项参数和我说的不一样

把截图发我，我告诉你点哪个。**不要自己改外设参数**——这些配置是队伍给好的，改错会导致电机不受控。

# GD32F450 SPI NAND 开发与验证项目

项目版本：**v1.0.0**。文档更新：**2026-11-04**。
目标为 GD32450Z-EVAL / GD32F450ZKT6，基于 GD32F4xx 标准外设库 V3.3.3、CMSIS 和 Arm GNU Toolchain，使用 CMake/Ninja 构建。
这是裸机驱动与验证工程，不是文件系统或完整 FTL。项目主要借助 AI 协作开发；后续开发应以源码、手册和可重复的验证结果为依据。

## 运行前必读

- 当前所有现有测试均开启。运行固件会擦写块 1～5，随后全片随机测试会擦写通过检查的所有块，原有业务数据将丢失。
- BBM demo 当前候选为 `LBA 7 -> PBA 1010`，执行开关为 `1U`。候选检查通过后会发送 A1h，建立掉电保留的映射并消耗内部表项。
- A1h **不会搬运数据**。它不是普通 RAM 配置，不要把反复运行 demo 当作无副作用操作。
- 本文不是对任意 NAND 或接线的兼容保证。首次部署应先阅读测试入口、调整开关，再考虑下载和继续执行。

## 硬件与接线

| 项目 | 当前配置 |
| --- | --- |
| 开发板 / MCU | GD32450Z-EVAL / GD32F450ZKT6 |
| 时钟 | 25 MHz 外部晶振，系统 200 MHz，APB2 默认 100 MHz |
| NAND 接口 | SPI5，GPIOG，AF5；`SPI_PSC_4`，默认 SCK 25 MHz |
| 日志串口 | USART0，115200，8 数据位、无校验、1 停止位；PA9 TX / PA10 RX |
| 调试 | GD-Link，CMSIS-DAP，SWD |
| LED 适配 | LED1 PD4、LED2 PD5、LED3 PG3 |

| NAND 信号 | MCU 引脚 |
| --- | --- |
| CS# | PG9 |
| SCK | PG13 |
| IO0 / MOSI | PG14 |
| IO1 / MISO | PG12 |
| IO2 / WP# | PG10 |
| IO3 / HOLD# | PG11 |

接线定义见 [inc/gd5f_spi_nand.h](inc/gd5f_spi_nand.h)。串口与 LED 定义见 [inc/gd32f450i_eval.h](inc/gd32f450i_eval.h)；文件名保留了原厂 I-EVAL 命名，但当前项目适配的是 Z-EVAL，不要仅凭文件名判断板型。
请按 NAND 完整型号和后缀核对电源、IO 电平及去耦，共地并缩短飞线。不能仅凭系列简称推断电压。四线错误也可能来自信号完整性，应结合降低 SCK 和波形测量排查。

## 支持的 NAND

| 型号 | ID | 主区容量 | 几何 | 当前扩展功能 |
| --- | --- | --- | --- | --- |
| GD5F1GM9 | `C8 91` | 1 Gb / 128 MiB | 1024 块，每块 64 页，每页 2048+128 B | Continuous Read、A1h/A5h BBM |
| GD5F4GQ6 | `C8 55` | 4 Gb / 512 MiB | 4096 块，每块 64 页，每页 2048+128 B | 普通单/双/四线驱动接口；当前器件表未启用 CR/BBM |

器件参数来源是 [src/gd5f_spi_nand.c](src/gd5f_spi_nand.c) 中的 `nand_table[]`。未知 ID 会返回通用 1 Gb 几何作为 fallback，**不等于已支持该器件**；全片测试会拒绝 `unknown`，但较早的五块 demo 仍有写操作，因此换芯片前必须先核对型号并调整测试入口。

## 环境与构建

1. 安装 VS Code、C/C++、CMake Tools、CMake 及 Ninja。工程最低要求 CMake 3.20；已使用 Arm GNU Toolchain 15.2.rel1 的 Windows `arm-none-eabi` 工具链构建。
2. 打开工程根目录。检查 [arm-none-eabi-gcc.cmake](arm-none-eabi-gcc.cmake) 的 `TOOLCHAIN_BIN_DIR`；当前默认是开发机的 `E:/feishu_cache/.../bin`，换电脑不能照搬。
3. 选择 `Debug` configure/build preset，通过 CMake Tools 构建，或在 GD32 DevSuite 中使用生成入口。配置来源见 [CMakePresets.json](CMakePresets.json)、[.project.json](.project.json)。
4. 确认输出位于 `build/Debug/`：`my-gd32-project.elf`、`.hex`、`.bin`、`.map` 和 `.lst`。更换工具链后应使用新配置目录或按工具提示重新配置，避免沿用不匹配的编译器缓存。

命令行部署参考，在工程根目录执行，并替换为实际安装路径：

```powershell
cmake --preset Debug "-DTOOLCHAIN_BIN_DIR=C:/Tools/ArmGNU/bin"
cmake --build --preset Debug
```

`Release` preset 也已定义，但当前仍使用 `-O0 -g`，不要误认为已经启用发布优化。
链接脚本 [ldscripts/gd32f4xx_flash.ld](ldscripts/gd32f4xx_flash.ld) 当前分配 Flash 512 KiB、RAM 192 KiB、TCMRAM 64 KiB；这是本工程的链接预算，不是对芯片物理总容量的声明。

## 下载、调试与日志

1. 接好 GD-Link 的 SWD、GND 和目标参考电压；按板卡说明供电。串口使用电平匹配的适配器，MCU TX 接适配器 RX。
2. 若使用 GD32 DevSuite，安装对应调试扩展，并核对 [.project.json](.project.json) 的 MCU、GD-Link 和 SWD 配置。
3. [.vscode/launch.json](.vscode/launch.json) 提供 `GD32 DevSuite (GD-Link)`、`GD32 Debug (OpenOCD)`、`GD32 Attach (OpenOCD)`。后两项需要 Cortex-Debug 和可用的 OpenOCD。
4. 修改 launch 配置中的 `gdbPath`、`serverpath` 等本机绝对路径。OpenOCD 启动配置使用 [openocd_gdlink/openocd_gdlink.cfg](openocd_gdlink/openocd_gdlink.cfg)：CMSIS-DAP/SWD，并用 `stm32f2x` Flash 驱动兼容 GD32F4；不要随意换成不存在的上游 `gd32f4xx` 驱动。
5. 先构建，再下载与符号匹配的 ELF。下载或启动调试可能复位并运行目标；需要保护 NAND 数据时应先修改测试入口，并在继续执行 `main` 前确认风险。
6. 打开正确串口，115200 8N1、无流控，查看芯片 ID、运行时核心/APB2/SCK 频率、PASS/FAIL、全片汇总和 BBM 表。Attach 配置用于连接已有目标，不能替代构建或自动证明固件与源码一致。

若 OpenOCD 预启动任务无法解析 `CMake: build`，先用 CMake Tools 完成构建，再检查当前 VS Code 的 CMake 任务提供器配置。不要通过更改硬件参数来掩盖环境配置问题。

## 源码导航与测试入口

| 入口 / 模块 | 职责 |
| --- | --- |
| [src/main.c](src/main.c) | 初始化串口、DWT 计时和 NAND；五块 demo、全片测试、BBM demo 按顺序运行 |
| [src/gd5f_spi_nand.c](src/gd5f_spi_nand.c) / [inc/gd5f_spi_nand.h](inc/gd5f_spi_nand.h) | 器件识别、SPI 传输、坏块标记、页与连续字节范围的擦写读接口 |
| [src/gd5f_spi_nand_cr.c](src/gd5f_spi_nand_cr.c) / [inc/gd5f_spi_nand_cr.h](inc/gd5f_spi_nand_cr.h) | GM9 Continuous Read 配置和单/四线连续读 |
| [src/gd5f_spi_nand_bbm.c](src/gd5f_spi_nand_bbm.c) / [inc/gd5f_spi_nand_bbm.h](inc/gd5f_spi_nand_bbm.h) | A5h 解析、候选检查、A1h 替换和前后表格输出 |
| [src/gd5f_spi_nand_full_test.c](src/gd5f_spi_nand_full_test.c) | 先扫描坏块标记，再擦除、伪随机写入、普通读与 CR 校验 |
| [src/gd32f450i_eval.c](src/gd32f450i_eval.c) | 串口及开发板外设适配 |
| [gcc_startup/startup_gd32f450_470.S](gcc_startup/startup_gd32f450_470.S) | 启动及中断向量 |
| [Firmware/](Firmware/) | 厂商 CMSIS、系统时钟与标准外设库 |

当前开关和默认值：

| 位置 | 配置 | 作用 |
| --- | --- | --- |
| `main.c` | `NAND_TEST_BLOCK=1U`、`NAND_TEST_BLOCK_COUNT=5U` | 五块擦写、普通读、单/四线 CR 及耗时统计；`nand_demo()` 无独立使能宏 |
| `main.c` | `NAND_FULL_TEST_ENABLE=1U` | 开启全片破坏性测试；设为 `0U` 只关闭这一项，不关闭五块 demo |
| `main.c` | `NAND_FULL_TEST_IO_MODE=SPI_NAND_IO_QUAD` | 全片写入方式，随后校验单线/四线普通读及支持的 CR |
| `gd5f_spi_nand_bbm.c` | `SPI_NAND_BBM_DEMO_EXECUTE=1U` | 候选合法时允许发送持久 A1h；设为 `0U` 只做读取与候选检查 |
| `gd5f_spi_nand_bbm.c` | `SPI_NAND_BBM_DEMO_LBA=7U`、`SPI_NAND_BBM_DEMO_PBA=1010U` | 当前 BBM demo 候选，不代表芯片实际已有映射 |

只想查看映射表时，必须同时禁用/跳过五块 demo、关闭全片测试，并关闭 BBM 执行。仅关闭全片测试不是只读模式。

## NAND 与 BBM 开发约束

- 普通接口不提供软件 FTL、磨损均衡、自动跳坏块或数据迁移。字节地址不包括 OOB，页号为 `block * pages_per_block + page_in_block`。编程前先擦除，并遵守器件的局部编程次数要求。
- “没有软件重映射”不代表可以绕过芯片 BBM：A1h 建立的硬件映射仍作用于设备收到的地址。全片测试自身不发送 A1h，但不会撤销已有映射，也不能保证遍历互相独立的原始物理块。
- 工厂坏块标记应在任何破坏性擦写前建立记录。当前驱动按器件表的页数/偏移检查标记；读取失败同样返回 ERROR。已启用硬件映射后，逻辑地址读到的标记可能不是原物理块的标记。
- 替换 PBA 必须为可用好块、不能与有效映射冲突，也不应含有需要保留的业务数据。替换池是软件规划，不是固定从块 1000 开始的硬件隐藏区。
- A5h 返回 20 项内部 link：状态 `00` 空闲、`10` 有效、`11` 已使用但失效，`01` 保留。每次成功 A1h 新增一个 link；失效记录仍消耗容量。手册未提供公开的清空/回收命令。
- 旧 PBA 复用已在 `4 -> 1004 -> 1005 -> 1004` 的实测中成功，第三次占用新表项。当前软件仅对有效表项拦截冲突，这是实验性放宽，不是任意复用的规格保证。
- GM9 手册第 89 页另有禁止重复 PBA 的注释，对失效历史的适用范围不明确。生产使用前须进一步向厂商确认，不应仅凭一次成功推广。
- 以每次 A5h 实读表为准；不要把历史日志的剩余项数、映射和候选参数写死为当前硬件状态。A1h 后 demo 会打印结果、状态寄存器及新的 A5h 表。

GM9 四线 Continuous Read 的 dummy 已按手册时序修正：`CRDC=0` 为 32 clocks，`CRDC=1` 为 24 clocks。不要混用普通 cache read 的 dummy 设置。
GM9 手册依据为 **DS-GD5F1GM9-Rev1.0，May 2025**，重点查阅 Continuous Read 时序及第 15 章（第 88～90 页）。手册未随仓库分发，需要自行取得匹配版本；其他型号必须查对应手册。

## 版本与后续开发

- `v1.0.0` 包含通用 NAND 驱动、CR、全片测试、BBM demo；代码功能基线提交为 `b6d7d66`。文档与注释修正使用独立提交，不混入运行逻辑修改。
- 既往硬件日志曾验证 GM9 单/四线普通读、CRDC 两种四线时序以及 BBM 表项更新。它们是指定硬件和配置的结果，不代替本次修改后的重新验证。
- 厂商版权年份及未修改的 SDK 版本记录保留原样；项目维护日期/版本与厂商库版本分开记录。当前项目维护年份为 2026，不表示原厂库在该日重新发布。
- 在 `debug` 做功能开发；每次提交详细说明目的、改动、验证和风险，合并前检查工作区及差异。文档提交使用 `docs:`，行为变化另行提交。未经明确要求不要改写提交或移动已有版本标签。
- 构建成功只证明编译/链接通过。硬件验证需单独说明器件、接线、时钟、测试范围和结果；未执行的项目必须明确标注。
- [.github/copilot-instructions.md](.github/copilot-instructions.md) 为 Copilot 的简短项目规则。给 AI 的任务建议包括目标行为、允许修改范围、验收命令，以及是否允许破坏性硬件操作；不要仅依赖聊天历史。

修改引脚、器件表、测试开关、工具链或 BBM 策略时，同步维护本文。不要把本机路径、一次性硬件状态或未经验证的推测当成通用部署条件。

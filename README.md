# GD32F450 SPI NAND 开发与验证项目

基线：**v1.0.0**；开发分支：**feature/ecc-bad-block**；更新：**2026-11-06**。
作者 / Flash FAE：[teng.wu@gigadevice.com](mailto:teng.wu@gigadevice.com)。
面向 GD32450Z-EVAL / GD32F450ZKT6 的裸机 NAND 驱动与测试，使用 GD32F4xx V3.3.3、CMSIS、C11 和 CMake/Ninja；不提供文件系统或 FTL。

## 运行前必读

- **默认启用定点擦写测试**，运行前应在测试配置头文件中确认范围。关闭全片测试不等于只读。
- 开启 BBM 执行会擦除目标并建立持久映射，消耗有限表项；A1h 本身不搬运数据。
- ECC 异常读取可能自动写入 OOB 坏块标记。烧录、复位或继续调试前，先确认测试范围和数据可丢弃。

## 硬件与接线

| 项目 | 配置 |
| --- | --- |
| 开发板 / MCU | GD32450Z-EVAL / GD32F450ZKT6 |
| 时钟 | 晶振 25 MHz，系统 200 MHz，APB2 100 MHz |
| NAND | SPI5 / AF5，`SPI_PSC_4`，默认 SCK 25 MHz |
| 引脚 | CS PG9、SCK PG13、IO0 PG14、IO1 PG12、IO2 PG10、IO3 PG11 |
| 日志 | USART0，PA9 TX / PA10 RX，115200 8N1、无流控 |
| 调试 | GD-Link，CMSIS-DAP，SWD |
| LED | PD4 / PD5 / PG3 |

NAND 引脚见 [inc/gd5f_spi_nand.h](inc/gd5f_spi_nand.h)，板级定义见 [inc/gd32f450i_eval.h](inc/gd32f450i_eval.h)；后者保留旧文件名，实际适配 Z-EVAL。按芯片完整型号核对电源和 IO 电平，共地并缩短飞线。

## 支持的 NAND

| 型号 | ID | 主区容量 / 块数 | 当前功能 |
| --- | --- | --- | --- |
| GD5F1GM9 | `C8 91` | 128 MiB / 1024 | 普通读写、Continuous Read、A1h/A5h BBM |
| GD5F4GQ6 | `C8 55` | 512 MiB / 4096 | 普通单/双/四线接口，未启用 CR/BBM |

两者均为每块 64 页、每页 2048+128 B。**CR 和 BBM 在当前支持器件中仅对 GD5F1GM9 启用**，不能推广到整个 GD5F 系列。扩展器件需核对对应手册及 `nand_table[]`；未知 ID 的 fallback 几何不代表兼容，不应直接运行写测试。

## 构建与部署

1. 安装 CMake 3.20+、Ninja、Arm GNU Toolchain（已用 15.2.rel1）；VS Code 使用 CMake Tools 或 GD32 DevSuite。
2. 在 [arm-none-eabi-gcc.cmake](arm-none-eabi-gcc.cmake) 配置 `TOOLCHAIN_BIN_DIR`，选择 `Debug` preset 构建。换电脑须调整本机路径，更换编译器后重新配置缓存。
3. 产物在 `build/Debug/`，包括 ELF、HEX、BIN、MAP、LST。用 GD-Link 下载 HEX，调试加载对应 ELF；串口使用上表参数。
4. 使用 [.vscode/launch.json](.vscode/launch.json) 前调整 `gdbPath` / `serverpath`。OpenOCD 方案需 Cortex-Debug，配置在 [openocd_gdlink/openocd_gdlink.cfg](openocd_gdlink/openocd_gdlink.cfg)，使用 `stm32f2x` Flash 驱动兼容 GD32F4。

命令行参考（根目录执行，替换工具链路径）：

```powershell
cmake --preset Debug "-DTOOLCHAIN_BIN_DIR=C:/Tools/ArmGNU/bin"
cmake --build --preset Debug
```

`Release` 当前仍为 `-O0 -g`，未启用优化。[链接脚本](ldscripts/gd32f4xx_flash.ld) 分配 Flash 512 KiB、RAM 192 KiB、TCMRAM 64 KiB，这是链接预算而非芯片总容量。

## 模块入口

| 入口 / 模块 | 职责 |
| --- | --- |
| [inc/gd5f_spi_nand_test_config.h](inc/gd5f_spi_nand_test_config.h) | 固件测试开关及参数的唯一配置入口 |
| [src/main.c](src/main.c) | 初始化、测试开关、五块读写与 CR 耗时统计 |
| [src/gd5f_spi_nand.c](src/gd5f_spi_nand.c) | 通用 SPI、器件表、页/字节接口、ECC 和 OOB 坏块标记 |
| [src/gd5f_spi_nand_cr.c](src/gd5f_spi_nand_cr.c) | GM9 单/四线 Continuous Read |
| [src/gd5f_spi_nand_bbm.c](src/gd5f_spi_nand_bbm.c) | A5h 表、候选检查、主区迁移/校验及 A1h |
| [src/gd5f_spi_nand_bad_block_test.c](src/gd5f_spi_nand_bad_block_test.c) | 独立坏块标记扫描，支持指定块范围 |
| [src/gd5f_spi_nand_full_test.c](src/gd5f_spi_nand_full_test.c) | 坏块预检、全片随机擦写及读取校验 |
| [host_tests/test_bbm_migration.c](host_tests/test_bbm_migration.c) | 主机模拟回归，不参与 MCU 固件构建 |

## 测试开关

统一修改 [inc/gd5f_spi_nand_test_config.h](inc/gd5f_spi_nand_test_config.h)，不要在各源文件另设测试宏。块号、范围、I/O 模式、缓冲区大小、随机种子和 BBM 候选都是测试参数，不是芯片固有属性；临时数值无需同步到 README。

| 测试项 | 独立开关 | 说明 |
| --- | --- | --- |
| 坏块标记扫描 | `NAND_BAD_BLOCK_CHECK_ENABLE` | 支持起始块及块数，不擦写数据 |
| 定点读写与 CR 计时 | `NAND_TIMING_TEST_ENABLE` | 擦写配置的块范围；CR 仅在支持器件上执行 |
| 全片随机测试 | `NAND_FULL_TEST_ENABLE` | 破坏性擦写全部通过预检的块 |
| BBM demo | `NAND_BBM_DEMO_ENABLE` | 读取 A5h 及检查候选；还需 `SPI_NAND_BBM_DEMO_EXECUTE` 才迁移和建立映射 |

开关互不隐含控制其他测试，执行顺序为标记扫描、定点测试、全片测试、BBM。只读表/检查时关闭定点、全片测试和 BBM 执行即可，不再使用 BBM-only 模式。扫描 ERROR 也可能是读取失败，不等于确认物理坏块。

## NAND 注意事项

- 页号为 `block * pages_per_block + page_in_block`，字节接口只含主区。无软件坏块跳过/重映射，但已有芯片 BBM 仍会重定向地址；映射后无法靠 LBA 标记恢复原物理块历史。
- BBM 流程：检查目标好块和表项 → 擦除目标 → 复制并校验整块主区 → 复查 A5h → 提交 A1h。不复制 OOB，需独占 NAND 和可丢弃的备用块；不保证掉电原子性。
- A5h 共 20 项：`00` 空闲、`10` 有效、`11` 已使用但失效。旧项不回收；A1h 结果不确定时先查表，不能盲目重试。历史 PBA 复用属实验行为，仍需厂商确认。
- ECC 在 PAGE READ 完成、cache 取数前检查。不可纠正错误及 GM9 的 `ECCS=11`（8-bit 已纠正，项目策略拒绝）会报 ERROR 并尝试持久写 OOB 坏块标记；写失败不能视为标记已保存，额外编程须遵守手册限制。
- GM9 四线 CR：`CRDC=0` 为 32 dummy clocks，`CRDC=1` 为 24。参考 **DS-GD5F1GM9-Rev1.0，May 2025**，CR 时序及第 15 章 BBM（第 88～90 页）；手册需自行取得。

## 主机回归与开发

主机测试使用模拟 NAND 和真实 BBM 实现，日志输出到终端；需要原生 GCC/Clang，不使用 `arm-none-eabi-gcc`：

```powershell
clang -std=c11 -Wall -Wextra -Werror -Ihost_tests/stubs -Iinc host_tests/test_bbm_migration.c src/gd5f_spi_nand_bbm.c -o build/bbm_migration_test.exe
./build/bbm_migration_test.exe
```

- WSL GCC 13.3 已通过 33 个迁移/故障场景、3 个地址检查及 ECC 阈值用例；不验证真实 OOB 编程或掉电一致性。
- 硬件 A/B 实测：映射后通过 LBA 改写 B，而直接读取替换地址仍为 A，支持地址交换式表现的假设，但不保证 PBA 可独立使用；临时实验代码已删除。
- 构建不等于硬件 PASS；在 `debug` 或功能分支开发，文档与代码分开提交，保留厂商版权/SDK 历史，未经授权不烧录、不擦写、不提交或推送。
- AI 开发规则见 [.github/copilot-instructions.md](.github/copilot-instructions.md)；接线、器件或默认值变化时同步本文，硬件状态以实时读取为准。

# 固件结构与工程约定（Keil C51）

## 文件结构

| 文件 | 职责 |
|------|------|
| main.c | 主流程：boot 顺序、alive 循环 |
| vlcd.c/.h | P1.1 PWM 端口与功能、VCC 测量（ADC15）、BGV 出厂值捕获（`VLCD_EarlyInit()`，**必须是 main 第一句**） |
| ht1621.c/.h | HT1621 总线端口初始化、命令序列与诊断 |
| buttons.c/.h | 按键初始化与 `Buttons_Read()`（位掩码：bit0=KEY_MAIN、bit1..5=KEY1..5、bit6=KEY6） |
| uart.c/.h | UART1 初始化与字节发送 |
| common.c/.h | 延时、`putchar`、`SleepForever` |
| timeslice.c/.h | 20ms 帧循环：WKT 唤醒、`g_cycle`、`Slice_SleepOneTick()`；`Timeslice_EarlyInit()`（main 第二句）快照 idata F8/F9 出厂 WKT 频率并算好重装值。STC8H 的 WKT 无中断向量，"醒来"即 tick——醒着时间不走，超片即掉帧（无积压），片内工作必须远短于 20ms |

端口模式/上拉配置一律用掩码操作（`|=` / `&= ~`）只动本模块的引脚位，不整写 PxM0/PxM1/PxPU；硬件级参数（主时钟 `MAIN_FOSC_HZ`、波特率 `UART_BAUDRATE`、过压阈值 `VCC_OVERVOLT_MV`）集中在 hardware-definition.h。`SleepForever()` 中对 PxM0/PxM1 的整体赋值是刻意为之（强制全部引脚回安全态），不在此例。

## 开机流程（当前 main.c 骨架）

1. `VLCD_EarlyInit()`（main 第一句，捕获 idata 出厂 BGV 镜像）；`Timeslice_EarlyInit()` 紧随其后（捕获 idata F8/F9 出厂 WKT 频率并算出 WKT 重装值）；随后 UART1 初始化；
2. **过压检查（开机第一项功能，趁 UART 刚好可以报告时立即执行）**：ADC15 自测 VCC，VCC ≥ `VCC_OVERVOLT_MV` → `SleepForever()`（IO 置安全态 + 全中断关闭 + 永久掉电 + 死循环兜底），此后不再复查过压；
3. 其余外设初始化（按键、HT1621 总线）；`VLCD_SetByVccMv()` 按开机 VCC 一次性设定 VLCD（含 ADC 失败 60% 下限兜底；`VLCD_Reapply()` 为"重测 VCC 再设定"的无参变种，供连续调节用，内含过压检查为无）；
4. `HT1621_SysInit()`：SYS_EN + RC_256K（LCD/蜂鸣的公共前置）；
5. `Timeslice_Init()`（IRCDB + WKT 20ms）后进入帧循环主循环：每片 `工作 → Slice_SleepOneTick()`（STOP 等唤醒，醒来 g_cycle++）。全固件无中断（EA=0），详见 [business-logic-log.md](business-logic-log.md)。

bring-up 阶段的诊断版 main（DATA 浮空测试、蜂鸣、全段点亮、READ 转储、alive 打印）存档于 [main-debug-c.txt](main-debug-c.txt)，不再参与编译。

**printf 链入效应**：清洁骨架 code=1276 / const=0 的前提是全固件无一处 printf——任何一条 printf 都会把 printf 本体（~860B）、C 库 long 运行时和整个模块常量段（?CO?模块 内所有字符串，HT1621 为 605B）一并链回，总增量约 +1.9KB。开发期临时打印可以接受，发布前移除。

## 存储器纪律（8051 空间模型）

- DATA 只有 128 字节：纯调试变量显式放 xdata，函数拆分让链接器 overlay；
- STARTUP.A51 `XDATALEN=0` 不清 XRAM：所有 xdata 变量必须先写后读；
- 时序敏感的循环变量（30000 次采样、HT1621 位敲）留在寄存器——插 movx 会歪时序。

## printf 红线（Keil C51）

所有参与打印的值先算好存局部变量，每次调用不超过 2 个 long 参数，也不在 printf 参数表里调用函数；`%bX/%bu` 用于 char，`%lu` 需显式 cast。

## VCC 自测量与 BGV

ADC15 固定接内部 1.19V 带隙，VREF+ = VCC，故 `N = BGV/VCC × 1024`，`VCC_mV = BGV_mV × 1024 / N`（手册 10.5 节反推法）。出厂校准镜像在高 idata（**完整地图在 hardware-definition.h**：0xEF/0xF0 BGV 大端 mV、0xF8/0xF9 WKT 时钟大端 Hz 标称 32768=8000H 出处 7.11.1+10.4.3、0xF1~0xF7 手册未描述）。STARTUP.A51 `IDATALEN=0x80` 只清 0x00–0x7F，镜像存活；固件在 `main()` 开头用 `VLCD_EarlyInit()` / `Timeslice_EarlyInit()` 抢在栈增长前快照到 xdata。BGV 数值不合理（超出 1000–1400mV）时回退标称 1190mV。

## P1.1 软件采样自检的分辨力

采样一次 ~1.4µs；50kHz 下低脉冲只有几个 PWM 计数宽（CCR=118 → 333ns）时永远采不到——近满占空比合法读到 0pm（CCR=118 理想 17pm），该自检只对 CCR ≲ 110 有意义。

## PWMA / XFR 访问

PWMA 寄存器是 XFR 映射，访问前需 `P_SW2 |= 0x80`（EAXFR）；`PWMA_CCR1` 等 16 位寄存器在 stc8h.h 中是 xdata 指针宏（如 0xFED5）。

## HT1621 时序

位敲半周期不用软件延时循环，直接由 NOP 构成：`HT1621_BitDelay()` 函数体内 `NOP40(),NOP8()`（STC8H.H 提供逗号链式 NOP1()..NOP40() 家族，不够长就按同样风格拼接），＝ 48 周期 = 8µs @6MHz 1T。**包成函数而非宏**：48 字节延迟体只存在一份，9 处调用各付 2 字节 LCALL（宏内联要 432 字节，函数版共 ~67 字节）；LCALL/RET 约 6 周期使半周期 ~9.5µs，RD 时钟 ≈52kHz，满足 WR ≤150kHz、RD ≤75kHz 的 datasheet 限制。读时 P1.7 切高阻、读完恢复推挽；数据位序（mode ID / 地址 MSB-first，数据 nibble LSB-first）见 ht1621.c 注释。

## 烧录注意

IRC 必须设 6MHz，否则波特率与延时不匹配（24MHz 下波特率误差可为 0.00%）。

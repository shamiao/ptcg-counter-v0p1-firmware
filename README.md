# ptcg-counter-v0p1

宝可梦集换式卡牌游戏（PTCG）对局计数器 —— v0.1（第一版硬件）。

基于 **STC8H1K08**（8051 内核，TSSOP-20）+ **HT1621B** 段码 LCD 驱动器的 3 位数字手持计数设备，带 7 个按键和蜂鸣器。

> **当前阶段：业务逻辑开发中。** 硬件 bring-up 已完成并逐项验证（详见 [dev-notes/bringup-log.md](dev-notes/bringup-log.md)）；当前固件为清洁骨架——仅初始化全部外设，业务逻辑待实现，见[路线图](#路线图)。

---

## 硬件

### MCU 与引脚分配（STC8H1K08，TSSOP-20）

| 引脚 | 功能 | 电气配置 |
|------|------|----------|
| P1.0 | KEY6（按键） | 外部上拉，内部上拉使能 |
| P1.1 | HT1621 VLCD 偏压 | 推挽，PWMA CH1 PWM1N 输出，1.8kΩ+1µF RC 滤波 |
| P1.2 | HT1621 /RD | 推挽 |
| P1.3 / P1.4 | NC | — |
| P1.5 | HT1621 /CS | 推挽 |
| P1.6 | HT1621 /WR | 推挽 |
| P1.7 | HT1621 DATA | 推挽（读时切高阻） |
| P3.0 / P3.1 | UART1 RXD / TXD | 准双向 |
| P3.2 | KEY_MAIN（按键，INT0） | 外部上拉，内部上拉使能 |
| P3.3 – P3.7 | KEY1 – KEY5（按键） | 外部上拉，内部上拉使能 |
| P5.4 | NC | — |

IRC 运行频率 **6MHz**（`MAIN_FOSC`，须与烧录工具中设置的 IRC 频率一致）。

### LCD 与段码映射

3 位数字液晶（第 1、2 位带小数点），1/4 duty、1/3 bias 驱动，实测驱动电压 ~3.2V。HT1621 RAM 地址 n 即 SEG n（0–31），nibble 的 D0–D3 对应 COM0–COM3：

| COM | SEG0 | SEG1 | SEG2 | SEG3 | SEG4 | SEG5 |
|-----|------|------|------|------|------|------|
| COM0 | 1A | 1C | 2A | 2C | 3A | 3C |
| COM1 | 1B | 1D | 2B | 2D | 3B | 3D |
| COM2 | 1F | 1E | 2F | 2E | 3F | 3E |
| COM3 | 1G | DP1 | 2G | DP2 | 3G | NC |

### 偏压与蜂鸣

P1.1 输出 50kHz PWM，经 1.8kΩ+1µF RC 滤波得到直流 VLCD ≈ 3.2V；开机按实测 VCC 一次性设定占空比，含 5.5V 过压门禁（方案细节见 [dev-notes/vlcd-bias.md](dev-notes/vlcd-bias.md)）。蜂鸣器使用 HT1621 片内音调发生器（TONE 2K/4K 命令），无需 MCU 侧 PWM。

---

## 构建与烧录

**工具链**：Keil µVision（C51 / MCS-51），工程文件 `ptcg-counter-v0p1.uvproj`（设备选 STC8H1K08 Series）。

- `#include <stc8h.h>` 使用 STC 官方头文件 `stc-pdf/STC8H.H`（需将其加入 Keil 包含路径，或复制到 Keil 的 INC 目录）；
- 命令行构建：`"C:/Keil_v5/UV4/UV4.exe" -b ptcg-counter-v0p1.uvproj -j0 -o build.log`；
- 编译产物：`Objects/ptcg-counter-v0p1.hex`（当前 code=1276 字节，约占 8K Flash 的 16%；const=0，data=19.0，xdata=6）。

**烧录**：STC-ISP（官方）或 [stcgal](https://github.com/stcgal/stcgal)，**IRC 频率设为 6MHz**。烧录后串口 9600 8N1 即可看到诊断输出。

---

## 目录结构

```
ptcg-counter-v0p1/
├── main.c                   # 应用固件主流程（外设初始化 + 业务逻辑位）
├── vlcd.c/.h                # VLCD 偏压 PWM、VCC 测量、BGV 出厂值捕获
├── ht1621.c/.h              # HT1621 驱动与诊断
├── buttons.c/.h             # 按键初始化与读取（位掩码）
├── uart.c/.h                # UART1 初始化与发送
├── common.c/.h              # 延时、putchar、SleepForever
├── hardware-definition.h    # 全局参数 + 引脚/LCD 映射（注释）
├── STARTUP.A51              # Keil 启动文件
├── ptcg-counter-v0p1.uvproj # Keil µVision 工程
├── dev-notes/               # 开发文档 + 调试固件存档（main-debug-c.txt）
├── Listings/  Objects/      # 编译列表与产物（gitignore）
├── stc-pdf/  stc-txt/       # 官方手册 PDF 与文本提取（gitignore）
└── temp/  build.log         # 台架串口日志、编译日志（gitignore）
```

---

## 开发文档

- [bringup-log.md](dev-notes/bringup-log.md) —— 台架验证记录、测量精度、已知问题
- [vlcd-bias.md](dev-notes/vlcd-bias.md) —— VLCD 偏压方案：拓扑、控制律、验收数据、历史注
- [firmware-conventions.md](dev-notes/firmware-conventions.md) —— 固件结构与 Keil C51 工程约定
- [main-debug-c.txt](dev-notes/main-debug-c.txt) —— bring-up 调试固件存档（不再编译）

## 路线图

1. 段码驱动函数（数字 → SEG RAM 写入）；
2. 按键扫描 + 去抖（KEY_MAIN/KEY1–6，KEY_MAIN 在 INT0 可用于唤醒）；
3. 计数应用逻辑与显示刷新；
4. 低功耗：空闲时 STC8H 掉电模式 + 按键唤醒、HT1621 LCD_ON/OFF、ADC 用完即关。

## 参考资料

- STC8H 系列手册（`stc-pdf/STC8H-*.pdf`，分章）
- HT1621/1621G v3.40 数据手册（`stc-pdf/HT1621_21Gv340.pdf`）
- UART 波特率与 ADC 反推 VCC 公式：STC8H 手册第 10.5 节

# 固件结构与工程约定（Keil C51）

## 文件结构

| 文件 | 职责 |
|------|------|
| main.c | 主流程：boot 顺序 + 帧循环骨架（采事件 → `App_Slice` 分发 → 心跳 → 睡）；业务逻辑全部在 app/mode_* 模块 |
| vlcd.c/.h | P1.1 PWM 端口与功能、VCC 测量（ADC15）、BGV 出厂值捕获（`VLCD_EarlyInit()`，**必须是 main 第一句**） |
| ht1621.c/.h | HT1621 总线端口初始化、命令序列与诊断 |
| buttons.c/.h | 按键初始化、`Buttons_Read()`（位掩码：bit0=KEY_MAIN/KEY0、bit1..5=KEY1..5、bit6=KEY6）、按键状态机 `Buttons_Scan()`（每片一步，返回新接受的单键掩码：S0 空闲 → 单键沿触发一次 → S1 持按无重复；任意态见 2+ 键 → S2 拦截、全松才回 S0。**消抖 = 20ms 采样节拍**，短于一片的抖动采不到；双触发需键抖 >20ms，属坏开关范畴，对比分析见 key-debounce-analysis.md）与 **KEY0 松开跟踪器 `Buttons_Key0Released()`**（双状态机的第二机：接受 KEY0 按下时武装，KEY0 松开触发一次——含 S2 内先松 KEY0 的情形；主 FSM 保持纯键数驱动不受影响）；`Buttons_Key0Disarm()` 吞掉待决松开事件（待机唤醒用，架构级屏蔽） |
| uart.c/.h | UART1 初始化与字节发送 |
| common.c/.h | 延时、`putchar`、`SleepForever` |
| timeslice.c/.h | 20ms 帧循环：WKT 唤醒、`g_cycle`、`Slice_SleepOneTick()`；`Timeslice_EarlyInit()`（main 第二句）快照 idata F8/F9 出厂 WKT 频率并算好重装值。STC8H 的 WKT 无中断向量，"醒来"即 tick——醒着时间不走，超片即掉帧（无积压），片内工作必须远短于 20ms。**不变量（2026-09-29 修订）：除待机深睡期间（INT0 空 ISR，EX0/EA 短暂使能）外全固件无中断（EA=0）**；待机深睡复用同一个 STOP 点，仅关 WKTEN 使其只能被 INT0 结束 |
| display.c/.h | 3 位段码显示：`g_disp_buf[3]`（个/十/百，bit7=EN bit6=DP bit0-5=字符码 0~35，`DISP_GLYPH('A')` 宏转换）+ code 区查表 `g_7segtab[3][36][2]`（tools/gen7segtab.py 生成，改字形/接线须重跑）+ `Display_Render()` 全面覆写渲染。字形严格按 dev-notes/7seg-charset.html |
| app.c/.h | UI 模式分发：`app_mode`（唯一全局）；`App_Slice(events)` 每片分发到当前模式（并递减进入 chirp 定时器，归零关蜂鸣）；`App_SwitchTo()` 切模式 + 调 enter 钩子 + **2K 进入短鸣（100ms，片驱动；APP_MODE_STANDBY 豁免——非业务模式）**；`App_BeepCancel()` 供接管蜂鸣器的流程作废 chirp；**空闲检测（`idle_slices` u16，任何键事件清零，1 分钟无操作 → 待机）**。**事件字**：bit0..6=被接受的单键掩码、bit7=`KEY_EV_K0UP`（KEY0 松开），同片可并发 |
| mode_count.c/.h | 模式 **1**（计数，业务默认）：饱和计数器，`count_val`（u16）模块私有；KEY1..6=±100/±10；**±100 越界即拒绝（只操作百位），±10 边界饱和；值动=接受（40ms 单鸣）、值不动=拒绝（3×20ms 超短鸣），键反馈蜂鸣为 ON/GAP 片驱动日程；Enter 清零自身鸣叫日程**；KEY0 按下→无条件切 APP_MODE_COIN |
| mode_coin.c/.h | 模式 **2**（投硬币）：仪式——摇动（80ms/面，"U P"/"DON"，允许轻微拖影）→ KEY0 松开锁定结果 → **按面分化的闪显**（UP：显隐显隐显 300ms 相×5=1.5s；DON：显隐显 500ms 相×3=1.5s；每次"显"伴 2K 蜂鸣，末相无缝流入长显）→ 5s 显 → 0.3s 隐 → 回计数模式。相位机 + 片倒计数全非阻塞，忽略一切按键事件，串口静默 |
| mode_standby.c/.h | 模式 **0**（待开机）：1 分钟无操作进入。SLEEP 相：HT1621 TONE_OFF→LCD_OFF→SYS_DIS 停振、P1.1 高阻、全端口整写准双向安全态（与 SleepForever 同类例外）、关 WKTEN、INT0 空 ISR 唤醒、STOP（复用 Slice_SleepOneTick 的唯一睡眠点；ArmDeepStop 含 IE0=0 清 pending 沿）。AWAIT 相：醒来当片恢复无中断不变量 + 重开 WKT，50 片窗口内 **KEY0 引脚级**松开检测（`(events&KEY_EV_K0UP)||(P3&0x04)`——引脚项承重：多按合并的唤醒按压武装不了跟踪器，且 EMI 假唤醒下一片即回睡）=误触 → SBS_REARM 宽限相（等一片让释放抖动结束再重武装 INT0；宽限到期 P3.2 低则开新窗口）。满 1 秒 → `Buttons_Key0Disarm()` → **`Standby_WakeComplete()`**：测 VCC 并串口上报 `vcc %umV`（两条唤醒路径都经此），然后 VCC 门禁（<2.75V `VCC_HALT_MV` → 串口提示 + SleepForever；<2.85V `VCC_LOWBAT_MV` → "LO.B"/"ATT"×3 + 黑屏动画 SBS_LOWBAT）→ ResumeInit → 计数模式（有 chirp）。**`Mode_Standby_Boot()`：冷开机直通 WakeComplete**（无长按窗口/无松开过滤），main 经 `App_BootViaStandby()` 进入。内存全程保留。抖动审计结论：唤醒侧无需容忍处理（20ms 节拍即容忍），详见 business-logic-log 步骤 15 |

端口模式/上拉配置一律用掩码操作（`|=` / `&= ~`）只动本模块的引脚位，不整写 PxM0/PxM1/PxPU；硬件级参数（主时钟 `MAIN_FOSC_HZ`、波特率 `UART_BAUDRATE`、过压阈值 `VCC_OVERVOLT_MV`、掉电门限 `VCC_HALT_MV`/`VCC_LOWBAT_MV`）集中在 hardware-definition.h。`SleepForever()` 与待机下电（`Standby_PowerDown`）中对 PxM0/PxM1 的整体赋值是刻意为之（强制全部引脚回安全态），不在此例。

**术语约定**：文档/交流中计数对象称 **DMG**（PTCG 中角色身上的数字是已受伤害值，不是血量）；**程序代码与注释一律不出现 HP/DMG**——固件视角它只是一个纯计数器（业务语义只活在文档里）。

**蜂鸣器所有权**（HT1621 TONE，全程片驱动无忙等）：app 层持有模式进入 chirp（100ms，`App_SwitchTo` 开鸣、`App_Slice` 递减关鸣）；count 模块持有键反馈日程（ON/GAP 状态机）；coin 模块在闪显相位直接控鸣。规则：**接管蜂鸣器者必须作废他人日程**——coin 锁面时 `App_BeepCancel()` 作废 app chirp；模式切换时 Enter 钩子清零自身日程（冻结的旧模式日程无害，坑在再进入时的残留）。同一时刻只有一个所有者。

## 开机流程（当前 main.c 骨架）

1. `VLCD_EarlyInit()`（main 第一句，捕获 idata 出厂 BGV 镜像）；`Timeslice_EarlyInit()` 紧随其后（捕获 idata F8/F9 出厂 WKT 频率并算出 WKT 重装值）；随后 UART1 初始化；
2. **过压检查（开机第一项功能，趁 UART 刚好可以报告时立即执行）**：ADC15 自测 VCC，VCC ≥ `VCC_OVERVOLT_MV` → `SleepForever()`（IO 置安全态 + 全中断关闭 + 永久掉电 + 死循环兜底），此后不再复查过压；
3. 其余外设初始化（按键、HT1621 总线）；`VLCD_SetByVccMv()` 按开机 VCC 一次性设定 VLCD（含 ADC 失败 60% 下限兜底；`VLCD_Reapply()` 为"重测 VCC 再设定"的无参变种，供连续调节用，内含过压检查为无）；
4. `HT1621_SysInit()`：SYS_EN + RC_256K（LCD/蜂鸣的公共前置）；`HT1621_LcdOn()`（BIAS+LCD_ON）后进入自检显示：**8.8.8 亮 0.5s（DelayMs 线性延时，无蜂鸣）→ 灭 0.3s**（LCD 保持 ON）；
5. `Timeslice_Init()`（IRCDB + WKT 20ms）后 `App_BootViaStandby()`：**冷开机经待机模式直通唤醒完成序列**（VCC 门禁：<2.75V halt / <2.85V 低电动画 → ResumeInit → 计数模式，有 chirp）。帧循环每片：`Buttons_Scan()` 掩码 + `Buttons_Key0Released()`（编入 bit7）→ `App_Slice(events)` 分发到当前模式（模式内一切流程非阻塞、片驱动；空闲 1 分钟 → 待机深睡）→ 每 5s（250 片）`ts N` 心跳 → `Slice_SleepOneTick()`（STOP 等唤醒，醒来 g_cycle++）。全固件无中断（EA=0，唯一例外是待机深睡期的 INT0 空 ISR），详见 [business-logic-log.md](business-logic-log.md)。

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

位敲半周期不用软件延时循环，直接由 NOP 构成：`HT1621_BitDelay()` 函数体内 `NOP40(),NOP8()`（STC8H.H 提供逗号链式 NOP1()..NOP40() 家族，不够长就按同样风格拼接），＝ 48 周期 = 8µs @6MHz 1T。**包成函数而非宏**：48 字节延迟体只存在一份，9 处调用各付 2 字节 LCALL（宏内联要 432 字节，函数版共 ~67 字节）；LCALL/RET 约 6 周期使半周期 ~9.5µs，RD 时钟 ≈52kHz，满足 WR ≤150kHz、RD ≤75kHz 的 datasheet 限制。读时 P1.7 切高阻、读完恢复推挽。**位序（台架实锤，2026-09-28）：mode ID / 地址 / 命令码 / 数据 nibble 一律 MSB-first（数据 D3..D0）**——手册时序图把数据标成 "D0 D1 D2 D3" 是著名陷阱，勿信；诊断图案必须含非对称数据才能验位序（全 0x0F 验不出来）。

## 烧录注意

IRC 必须设 6MHz，否则波特率与延时不匹配（24MHz 下波特率误差可为 0.00%）。

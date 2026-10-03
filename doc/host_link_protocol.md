# HL1 通信协议 / HL1 通信プロトコル

PC 上位机与 RX71M、RA8P 使用同一套私有帧。帧内载荷是 Protocol Buffers 36.2（2026-09-17）的 proto3 编码。

PC ホストと RX71M、RA8P は同一の独自フレームを使う。フレーム内ペイロードは Protocol Buffers 36.2（2026-09-17）の proto3 符号化である。

模式来源 / 定義の所在：

- 帧：`protocol/host_link.h`、`protocol/host_link.c`
- 载荷：`protocol/host_link.proto`（`package renesas.hostlink`）

## 1. 物理层 / 物理層

业务串口，115200 8N1，无校验，无流控。日志串口只输出文本，不走本协议。

業務 UART。115200 8N1、パリティなし、フロー制御なし。ログ用 UART はテキストのみで、本プロトコルは通さない。

| 板卡 / 基板 | 业务串口 / 業務 UART | TX | RX |
|---|---|---|---|
| RX71M | SCI2 | P50 | P52 |
| RA8P | SCI7 | P809 | P808 |

交叉连接：PC 的 TX 接板卡 RX，PC 的 RX 接板卡 TX，共地。

クロス接続とする。PC の TX を基板の RX に、PC の RX を基板の TX に繋ぎ、GND を共通にする。

## 2. 私有帧 HL1 / 独自フレーム HL1

多字节整数均为小端。载荷最长 192 字节，整帧最长 202 字节。

多バイト整数はリトルエンディアン。ペイロードは最大 192 バイト、フレーム全体は最大 202 バイト。

| 偏移 / オフセット | 长度 / 長さ | 字段 / フィールド | 说明 / 説明 |
|---|---|---|---|
| 0 | 1 | 魔数 0 | 固定 `0xA5` |
| 1 | 1 | 魔数 1 | 固定 `0x5A` |
| 2 | 1 | 版本 / バージョン | 当前为 `1` |
| 3 | 1 | 板卡号 / 基板番号 | 见下表 |
| 4 | 1 | 标志 / フラグ | 见下表 |
| 5 | 1 | 序号低 8 位 / シーケンス下位 8 ビット | 与 Envelope.sequence 的低 8 位相同 |
| 6 | 2 | 载荷长度 / ペイロード長 | 小端，不含本字段和 CRC |
| 8 | N | 载荷 / ペイロード | `Envelope` 的 protobuf 编码 |
| 8+N | 2 | CRC16 | 小端 |

板卡号 / 基板番号：

| 值 / 値 | 含义 / 意味 |
|---|---|
| 0 | 不限。两块板都接受 / 制限なし。両基板とも受信する |
| 1 | RX71M |
| 2 | RA8P |

标志 / フラグ：

| 位 / ビット | 名称 | 含义 / 意味 |
|---|---|---|
| bit0 | `HL_FLAG_FROM_BOARD` | 1 表示板卡发出 / 1 は基板発 |
| bit1 | `HL_FLAG_CAN_FD` | 1 表示该板支持 CAN FD / 1 は CAN FD 対応 |

PC 发出的帧：标志为 0。板卡号为 0、1 或 2。板卡只处理板卡号为 0 或等于自身编号的帧，其余丢弃且不回答。

PC 発フレームのフラグは 0。基板番号は 0、1、2 のいずれか。基板は、基板番号が 0 または自番号のフレームだけ処理し、それ以外は破棄して応答しない。

板卡发出的帧：bit0 置 1。RA8P 同时把 bit1 置 1，RX71M 不置 bit1。

基板発フレームは bit0 を 1 にする。RA8P は bit1 も 1 にし、RX71M は bit1 を立てない。

### CRC

CRC-16/CCITT-FALSE。

- 多项式 / 多項式：`0x1021`
- 初值 / 初期値：`0xFFFF`
- 输入、输出均不反转 / 入力・出力とも反転しない
- 结果异或 / 最終 XOR：`0x0000`
- 覆盖范围 / 計算範囲：版本字节到载荷末尾，不含魔数和 CRC 自身
- 线上顺序 / 送出順：低字节在前

CRC 错误的帧丢弃，不作为应答。

CRC 不一致のフレームは破棄し、応答には使わない。

## 3. Protobuf 载荷 / Protobuf ペイロード

编译器：protoc 36.2。语法：`syntax = "proto3"`。包名：`renesas.hostlink`。消息名：`Envelope`。

コンパイラは protoc 36.2。構文は `syntax = "proto3"`。パッケージ名は `renesas.hostlink`。メッセージ名は `Envelope`。

值为 0 仍有意义的控制字段使用 `optional`，因此 0 也会出现在线上。状态量仍按 proto3 习惯省略 0。

0 でも意味がある制御フィールドは `optional` とし、0 も回線上に出す。状態量は proto3 の慣例どおり 0 を省略する。

未知字段必须跳过，不能因此判帧非法。CAN 数据超过 8 字节则整条 `Envelope` 视为非法。

未知フィールドは読み飛ばす。それだけでフレームを不正にしてはならない。CAN データが 8 バイトを超える `Envelope` は不正とする。

### 3.1 Envelope

| 字段号 / フィールド番号 | 名称 | 类型 / 型 | 何时出现 / 出現条件 |
|---|---|---|---|
| 1 | sequence | uint32 | 非 0 时。PC 从 1 递增；板卡主动上报从 `0x80000000` 递增 |
| 2 | command | Command | 非 0 时 |
| 3 | snapshot | Snapshot | 状态应答或周期上报 |
| 4 | set_outputs | SetOutputs | 设置 GPIO 输出 |
| 5 | set_dac | SetDac | 设置 DAC |
| 6 | set_pwm | SetPwm | 设置 PWM |
| 7 | send_can | SendCan | PC 请求发 CAN |
| 8 | can_log | CanLog | 板卡上报已发送的 CAN |
| 9 | ack | Ack | 写操作的应答 |

一条载荷可以同时带 `ack` 与 `snapshot`，或只带其中之一。`send_can` 成功时，板卡先回 `ack`，再另发一帧 `can_log`。

1 つのペイロードに `ack` と `snapshot` を同時に載せてよい。`send_can` が成功したとき、基板は先に `ack` を返し、続けて別フレームで `can_log` を送る。

### 3.2 Command

| 值 / 値 | 名称 | 方向 / 方向 | 板卡行为 / 基板の動作 |
|---|---|---|---|
| 0 | COMMAND_UNSPECIFIED | — | 不作为请求 / 要求には使わない |
| 1 | COMMAND_PING | PC → 板 | 回 Snapshot / Snapshot を返す |
| 2 | COMMAND_SNAPSHOT | PC → 板 | 回 Snapshot / Snapshot を返す |
| 3 | COMMAND_SET_OUTPUTS | PC → 板 | 写 GPIO，回 Ack / GPIO を書き Ack を返す |
| 4 | COMMAND_SET_DAC | PC → 板 | 写 DAC，回 Ack / DAC を書き Ack を返す |
| 5 | COMMAND_SET_PWM | PC → 板 | 写 PWM，回 Ack / PWM を書き Ack を返す |
| 6 | COMMAND_SEND_CAN | PC → 板 | 发 CAN，回 Ack；成功后再报 CanLog |

### 3.3 Snapshot

| 字段号 | 名称 | 含义 / 意味 |
|---|---|---|
| 1 | tick | 板卡周期计数，约每 500 ms 加 1 / 基板の周期カウンタ。約 500 ms ごとに +1 |
| 2 | adc0 | 第一路 AD / AD チャネル 0 |
| 3 | adc1 | 第二路 AD / AD チャネル 1 |
| 4 | inputs | GPIO 输入，bit0–bit3 / GPIO 入力 bit0–bit3 |
| 5 | outputs | GPIO 输出，bit0–bit3 / GPIO 出力 bit0–bit3 |
| 6 | dac | 当前 DAC 码，0–4095 / 現在の DAC コード |
| 7 | board | `BOARD_KIND_RX71M=1`，`BOARD_KIND_RA8P=2` |
| 8 | can_fd | 该板能否发 CAN FD。RX71M 省略此字段；RA8P 为 true |
| 9 | name | `RX71M` 或 `RA8P1` |

### 3.4 控制消息 / 制御メッセージ

SetOutputs.mask：0–15，bit0–bit3 对应四路输出。0 表示全关，仍必须编码。

SetOutputs.mask は 0–15。bit0–bit3 が 4 本の出力に対応する。0 は全オフであり、省略せず符号化する。

SetDac.code：0–4095，12 位。0 仍必须编码。

SetDac.code は 0–4095 の 12 ビット。0 も省略せず符号化する。

SetPwm：

| 字段 | 范围 / 範囲 | 说明 / 説明 |
|---|---|---|
| channel | 0–3 | 四路 PWM / PWM 4 チャネル |
| duty_permille | 0–1000 | 占空比，单位千分比。500 表示 50% / デューティ比。単位はパーミル。500 は 50% |

channel 与 duty 为 0 时仍必须编码。

channel と duty が 0 でも省略せず符号化する。

SendCan / CanLog：

| 字段 | 说明 / 説明 |
|---|---|
| channel | 0 或 1。0 也必须编码 / 0 または 1。0 も符号化する |
| id | 11 位标准帧，0–`0x7FF` / 11 ビット標準フレーム |
| data | 1–8 字节。0 字节视为参数错误 / 1–8 バイト。0 バイトは引数エラー |
| fd | false 为经典 CAN，true 为 CAN FD。两种取值都编码 / false はクラシック CAN、true は CAN FD。両方とも符号化する |
| tx | 仅 CanLog。当前上报的都是板卡发送 / CanLog のみ。現在の通知は基板送信のみ |

RX71M 收到 `fd=true` 时不发送，Ack 状态为不支持，`detail` 为 `can fd unsupported`。RA8P 按 CAN FD 帧发送。两块板的自发 CAN 仍是经典帧。

RX71M は `fd=true` を受信しても送信しない。Ack は非対応で、`detail` は `can fd unsupported`。RA8P は CAN FD フレームとして送信する。両基板が自発的に出す CAN はクラシックフレームのままである。

### 3.5 Ack

| 字段 | 说明 / 説明 |
|---|---|
| command | 被应答的命令号 / 応答対象のコマンド番号 |
| status | 0 成功，1 不支持，2 参数错误 / 0 成功、1 非対応、2 引数エラー |
| detail | ASCII。成功时省略 / 成功時は省略 |

| detail | 含义 / 意味 |
|---|---|
| `bad protobuf` | 载荷无法按 Envelope 解码 / Envelope として復号できない |
| `bad outputs` | GPIO 掩码大于 15，或缺少字段 / マスクが 15 を超える、またはフィールド不足 |
| `bad dac` | DAC 大于 4095，或缺少字段 |
| `bad pwm` | 通道或占空比越界 |
| `bad can` | 通道、ID、长度不合法 |
| `can fd unsupported` | RX71M 拒绝 CAN FD |
| `can busy` | 发送邮箱忙 / 送信バッファが使用中 |
| `unknown command` | 命令号未定义 |

status 为 0 时，该字段按 proto3 省略。

status が 0 のとき、このフィールドは proto3 に従い省略する。

## 4. 时序 / タイミング

建立连接前，板卡不在业务串口上发送二进制帧。PC 打开串口后发送 `COMMAND_PING`。板卡收到第一帧合法且板卡号匹配的请求后，置连接已建立，并开始周期上报。

接続前、基板は業務 UART にバイナリフレームを出さない。PC はポートを開いたあと `COMMAND_PING` を送る。基板は、正当で基板番号が一致する最初の要求を受けた時点で接続済みとし、周期通知を始める。

| 动作 / 動作 | 周期 / 周期 |
|---|---|
| 板卡读串口 / 基板が UART を読む | 约 20 ms |
| 板卡采样并上报 Snapshot、自发 CAN 的 CanLog | 约 500 ms，且仅在已连接后 |
| PC 读串口 | 50 ms |
| PC 在未收到应答时重发 PING | 约 1 s |
| PC 在已连接后请求 SNAPSHOT | 约 1 s |

PC 主动请求使用的 sequence 从 1 递增，并写入帧序号的低 8 位。板卡应答沿用该 sequence。板卡主动上报使用独立序号，从 `0x80000000` 递增，避免与 PC 序号相混。

PC 発の sequence は 1 から増加し、フレームシーケンスの下位 8 ビットにも書く。基板の応答はその sequence を返す。基板の自発通知は `0x80000000` から増加する別シーケンスとし、PC の番号と混ぜない。

连接建立后，板卡不再用演示循环改写 GPIO、DAC、PWM。ADC、输入和自发 CAN 仍继续更新。

接続後、基板はデモ循環で GPIO、DAC、PWM を上書きしない。AD、入力、自発 CAN は更新を続ける。

## 5. 两块板的差异 / 2 基板の差分

帧格式、命令号和字段号相同。差异只在能力。

フレーム形式、コマンド番号、フィールド番号は同じである。違いは能力だけである。

| 项目 / 項目 | RX71M | RA8P |
|---|---|---|
| 板卡号 / 基板番号 | 1 | 2 |
| 名称 / 名前 | `RX71M` | `RA8P1` |
| CAN | 经典 CAN，两路，500 kbit/s | CAN FD，两路，仲裁段 500 kbit/s |
| 标志 bit1 | 0 | 1 |
| Snapshot.can_fd | 省略（即 false） | true |
| `fd=true` | Ack = 不支持 | 发送 CAN FD |

## 6. PC 操作与报文 / PC 操作と電文

| 操作 / 操作 | 发送 / 送信 |
|---|---|
| 连接 / 接続 | `command=PING` |
| 读取状态 / 状態読取 | `command=SNAPSHOT` |
| 下发输出 / 出力設定 | 连续三帧：`SET_OUTPUTS`、`SET_DAC`、`SET_PWM` |
| 发送 CAN / CAN 送信 | `command=SEND_CAN`，并填写 `send_can` |

目标板选“自动”时，帧内板卡号为 0。选定某一块板时，只把帧发给该编号；另一块板会丢弃。

対象基板が「自動」のとき、フレームの基板番号は 0。特定の基板を選んだときはその番号だけを送り、もう一方の基板は破棄する。

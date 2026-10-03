# HL1 通信プロトコル

PC ホストと RX71M、RA8P は同一の独自フレームを使う。フレーム内ペイロードは Protocol Buffers 36.2（2026-09-17）の proto3 符号化である。

中国語版は `host_link_protocol.md` を参照する。

定義の所在：

- フレーム：`protocol/host_link.h`、`protocol/host_link.c`
- ペイロード：`protocol/host_link.proto`（`package renesas.hostlink`）
- PC 側の独立実装：`PC_Host/src/uHostLink.pas`。バイト列は上記 C 実装および protoc 36.2 と一致させる

## 1. 物理層

業務 UART。115200 8N1、パリティなし、フロー制御なし。ログ用 UART はテキストのみで、本プロトコルは通さない。

| 基板 | 業務 UART | TX | RX |
|---|---|---|---|
| RX71M | SCI2 | P50 | P52 |
| RA8P | SCI7 | P809 | P808 |

クロス接続とする。PC の TX を基板の RX に、PC の RX を基板の TX に繋ぎ、GND を共通にする。ホストは一度に COM ポートを一つだけ開き、どちらか一方の基板の業務 UART に接続する。

## 2. 独自フレーム HL1

多バイト整数はリトルエンディアン。ペイロードは最大 192 バイト、フレーム全体は最大 202 バイト。

| オフセット | 長さ | フィールド | 説明 |
|---|---|---|---|
| 0 | 1 | マジック 0 | 固定 `0xA5` |
| 1 | 1 | マジック 1 | 固定 `0x5A` |
| 2 | 1 | バージョン | 現在は `1` |
| 3 | 1 | 基板番号 | 下表 |
| 4 | 1 | フラグ | 下表 |
| 5 | 1 | シーケンス下位 8 ビット | Envelope.sequence の下位 8 ビットと同一 |
| 6 | 2 | ペイロード長 | リトルエンディアン。本フィールドと CRC は含まない |
| 8 | N | ペイロード | `Envelope` の protobuf 符号化 |
| 8+N | 2 | CRC16 | リトルエンディアン |

基板番号：

| 値 | 意味 |
|---|---|
| 0 | 制限なし。両基板とも受信する |
| 1 | RX71M |
| 2 | RA8P |

フラグ：

| ビット | 名称 | 意味 |
|---|---|---|
| bit0 | `HL_FLAG_FROM_BOARD` | 1 は基板発 |
| bit1 | `HL_FLAG_CAN_FD` | 1 は CAN FD 対応 |

PC 発フレームのフラグは 0。基板番号は 0、1、2 のいずれか。基板は、基板番号が 0 または自番号のフレームだけ処理し、それ以外は破棄して応答しない。

基板発フレームは bit0 を 1 にする。RA8P は bit1 も 1 にし、RX71M は bit1 を立てない。

### CRC

CRC-16/CCITT-FALSE。

- 多項式：`0x1021`
- 初期値：`0xFFFF`
- 入力・出力とも反転しない
- 最終 XOR：`0x0000`
- 計算範囲：バージョンバイトからペイロード末尾まで。マジックと CRC 自身は含まない
- 送出順：下位バイトが先

CRC 不一致のフレームは破棄し、応答には使わない。

## 3. Protobuf ペイロード

コンパイラは protoc 36.2。構文は `syntax = "proto3"`。パッケージ名は `renesas.hostlink`。メッセージ名は `Envelope`。

両 MCU は libprotobuf をリンクしない。C と Pascal が、同じ proto3 ワイヤ形式をそれぞれ実装する。

0 でも意味がある制御フィールドは `optional` とし、0 も回線上に出す。状態量は proto3 の慣例どおり 0 を省略する。

未知フィールドは読み飛ばす。それだけでフレームを不正にしてはならない。CAN データが 8 バイトを超える `Envelope` は不正とする。

### 3.1 Envelope

| フィールド番号 | 名称 | 型 | 出現条件 |
|---|---|---|---|
| 1 | sequence | uint32 | 0 でないとき。PC は 1 から増加。基板の自発通知は `0x80000000` から増加 |
| 2 | command | Command | 0 でないとき |
| 3 | snapshot | Snapshot | 状態応答または周期通知 |
| 4 | set_outputs | SetOutputs | GPIO 出力の設定 |
| 5 | set_dac | SetDac | DAC の設定 |
| 6 | set_pwm | SetPwm | PWM の設定 |
| 7 | send_can | SendCan | PC が CAN 送信を要求 |
| 8 | can_log | CanLog | 基板が送信済み CAN を通知 |
| 9 | ack | Ack | 書き込み操作への応答 |

1 つのペイロードに `ack` と `snapshot` を同時に載せてよい。`send_can` が成功したとき、基板は先に `ack` を返し、続けて別フレームで `can_log` を送る。

### 3.2 Command

| 値 | 名称 | 方向 | 基板の動作 |
|---|---|---|---|
| 0 | COMMAND_UNSPECIFIED | — | 要求には使わない |
| 1 | COMMAND_PING | PC → 基板 | Snapshot を返す |
| 2 | COMMAND_SNAPSHOT | PC → 基板 | Snapshot を返す |
| 3 | COMMAND_SET_OUTPUTS | PC → 基板 | GPIO を書き Ack を返す |
| 4 | COMMAND_SET_DAC | PC → 基板 | DAC を書き Ack を返す |
| 5 | COMMAND_SET_PWM | PC → 基板 | PWM を書き Ack を返す |
| 6 | COMMAND_SEND_CAN | PC → 基板 | CAN を送り Ack を返す。成功後に CanLog を通知する |

### 3.3 Snapshot

| フィールド番号 | 名称 | 意味 |
|---|---|---|
| 1 | tick | 基板の周期カウンタ。約 500 ms ごとに +1 |
| 2 | adc0 | AD チャネル 0 |
| 3 | adc1 | AD チャネル 1 |
| 4 | inputs | GPIO 入力 bit0–bit3 |
| 5 | outputs | GPIO 出力 bit0–bit3 |
| 6 | dac | 現在の DAC コード。0–4095 |
| 7 | board | `BOARD_KIND_RX71M=1`、`BOARD_KIND_RA8P=2` |
| 8 | can_fd | その基板が CAN FD を送れるか。RX71M はこのフィールドを省略する。RA8P は true |
| 9 | name | `RX71M` または `RA8P1` |

### 3.4 制御メッセージ

SetOutputs.mask は 0–15。bit0–bit3 が 4 本の出力に対応する。0 は全オフであり、省略せず符号化する。

SetDac.code は 0–4095 の 12 ビット。0 も省略せず符号化する。

SetPwm：

| フィールド | 範囲 | 説明 |
|---|---|---|
| channel | 0–3 | PWM 4 チャネル |
| duty_permille | 0–1000 | デューティ比。単位はパーミル。500 は 50% |

channel と duty が 0 でも省略せず符号化する。

SendCan / CanLog：

| フィールド | 説明 |
|---|---|
| channel | 0 または 1。0 も符号化する |
| id | 11 ビット標準フレーム。0–`0x7FF` |
| data | 1–8 バイト。0 バイトは引数エラー |
| fd | false はクラシック CAN、true は CAN FD。両方とも符号化する |
| tx | CanLog のみ。現在の通知は基板送信のみ |

RX71M は `fd=true` を受信しても送信しない。Ack は非対応で、`detail` は `can fd unsupported`。RA8P は CAN FD フレームとして送信する。両基板が自発的に出す CAN はクラシックフレームのままである。ID は `0x123` と `0x321`、データは周期カウンタ 1 バイト、500 kbit/s。

### 3.5 Ack

| フィールド | 説明 |
|---|---|
| command | 応答対象のコマンド番号 |
| status | 0 成功、1 非対応、2 引数エラー |
| detail | ASCII。成功時は省略 |

| detail | 意味 |
|---|---|
| `bad protobuf` | Envelope として復号できない |
| `bad outputs` | マスクが 15 を超える、またはフィールド不足 |
| `bad dac` | DAC が 4095 を超える、またはフィールド不足 |
| `bad pwm` | チャネルまたはデューティが範囲外 |
| `bad can` | チャネル、ID、長さが不正 |
| `can fd unsupported` | RX71M が CAN FD を拒否 |
| `can busy` | 送信バッファが使用中 |
| `unknown command` | コマンド番号が未定義 |

status が 0 のとき、このフィールドは proto3 に従い省略する。

## 4. タイミング

接続前、基板は業務 UART にバイナリフレームを出さない。PC はポートを開いたあと `COMMAND_PING` を送る。基板は、正当で基板番号が一致する最初の要求を受けた時点で接続済みとし、周期通知を始める。

| 動作 | 周期 |
|---|---|
| 基板が UART を読む | 約 20 ms |
| 基板がサンプリングし、Snapshot と自発 CAN の CanLog を通知する | 約 500 ms。接続後のみ |
| PC が UART を読む | 50 ms |
| 応答がない間、PC が PING を再送する | 約 1 s |
| 接続後、PC が SNAPSHOT を要求する | 約 1 s |

PC 発の sequence は 1 から増加し、フレームシーケンスの下位 8 ビットにも書く。基板の応答はその sequence を返す。基板の自発通知は `0x80000000` から増加する別シーケンスとし、一周したあとも `0x80000000` に戻す。PC の番号と混ぜない。

接続後、基板はデモ循環で GPIO、DAC、PWM を上書きしない。AD、入力、自発 CAN は更新を続ける。

## 5. 2 基板の差分

フレーム形式、コマンド番号、フィールド番号は同じである。違いは能力だけである。

| 項目 | RX71M | RA8P |
|---|---|---|
| 基板番号 | 1 | 2 |
| 名前 | `RX71M` | `RA8P1` |
| CAN | クラシック CAN、2 チャネル、500 kbit/s | CAN FD、2 チャネル、アービトレーション 500 kbit/s |
| フラグ bit1 | 0 | 1 |
| Snapshot.can_fd | 省略（false と同じ） | true |
| `fd=true` | Ack = 非対応 | CAN FD を送信 |

## 6. PC 操作と電文

| 操作 | 送信 |
|---|---|
| 接続 | `command=PING` |
| 状態読取 | `command=SNAPSHOT` |
| 出力設定 | 連続 3 フレーム：`SET_OUTPUTS`、`SET_DAC`、`SET_PWM` |
| CAN 送信 | `command=SEND_CAN`。`send_can` を入れる |

対象基板が「自動」のとき、フレームの基板番号は 0。特定の基板を選んだときはその番号だけを送り、もう一方の基板は破棄する。

# システムプラットフォームのソフトウェア構成

本文は、リポジトリに実装済みのソフトウェアを説明する。委託要求図の目標形態ではない。範囲は PC ホスト、共通 HL1 プロトコル、RX71M と RA8P の二つのファームウェアである。

日本語版の対になる中国語版は `software_architecture.md` を参照する。

プロトコル本文は `host_link_protocol_ja.md`。ピン対応は `mcu_resource_map.md`。

## 1. リポジトリ構成

| ディレクトリ | 役割 |
|---|---|
| `PC_Host` | Lazarus 4.8 / FPC 3.2.2 のホスト。フォーム、シリアル、プロトコル符号化 |
| `protocol` | フレームと proto3 の唯一の定義。`.proto`、C 実装、C 自己試験 |
| `POC_RX71M` | RX71M ファームウェア。GCC for Renesas RX、Smart Configurator、FreeRTOS |
| `POC_RA8P` | RA8P ファームウェア。LLVM Arm（ATfE）、FSP、FreeRTOS |
| `doc` | 要求、リソース表、プロトコル、本文 |

二つのファームウェアは libprotobuf をリンクしない。`#include "../../../protocol/host_link.c"` で同じ C を `board_resources.cpp` に取り込む。PC は Pascal で同じ形式を別実装し、起動時に `HostLinkMatchesProtoc` で protoc 36.2 の基準バイト列と照合する。

## 2. 実行時のコンテキスト

ホストは COM ポートを一度に一つだけ開き、一枚の基板の業務 UART に接続する。ログ用 UART は人が読むテキストだけで、本プロトコルは通さない。

```mermaid
flowchart LR
  subgraph host [PC ホスト]
    gui[メインフォーム]
    codec[HL1 と proto3]
    com[業務シリアル]
    gui --> codec --> com
  end

  subgraph rx [RX71M]
    rxApp[main_task]
    rxIo[クラシック CAN / GPIO / AD / DA / PWM]
    rxLog[ログ SCI1]
    rxApp --> rxIo
    rxApp --> rxLog
  end

  subgraph ra [RA8P]
    raApp[new_thread0]
    raIo[CAN FD / GPIO / AD / DA / PWM]
    raLog[ログ SCI8]
    raApp --> raIo
    raApp --> raLog
  end

  com <-->|"115200 8N1 業務 UART"| rxApp
  com <-->|"115200 8N1 業務 UART"| raApp
```

同時に繋ぐのはどちらか一方である。フレーム内の基板番号が宛先を示す。0 は制限なし、1 は RX71M、2 は RA8P。基板は、0 でも自番号でもないフレームを破棄し、応答しない。

## 3. 論理構成

PC、共通プロトコル、二つの基板プログラムは実装が三系統で、回線上の形式は一つである。

```mermaid
flowchart TB
  subgraph pc [PC_Host]
    lpr[pc_host.lpr]
    main[uMain.pas]
    dpi[uDpi.pas]
    port[uComPort.pas]
    pas[uHostLink.pas]
    lpr --> main
    main --> dpi
    main --> port
    main --> pas
  end

  subgraph spec [protocol]
    proto[host_link.proto]
    cimpl[host_link.c / host_link.h]
    check[selftest.c]
    proto -.-> cimpl
    proto -.-> pas
    cimpl --> check
  end

  subgraph fw [二つのファームウェア]
    entry[FreeRTOS アプリケーションタスク]
    board[board_resources.cpp]
    entry --> board
    board --> cimpl
  end
```

### 3.1 ホスト

| ユニット | 役割 |
|---|---|
| `pc_host.lpr` | LCL スケーリングを有効にし、メインフォームを生成する |
| `uMain.pas` | 接続、設定送信、ログ、50 ms 周期のポーリング |
| `uDpi.pas` | 96 PPI 基準の幅を現在のフォームへ換算する |
| `uComPort.pas` | Windows シリアル。115200 8N1。読み出しは即時復帰 |
| `uHostLink.pas` | フレーム組立と分解、proto3 符号化、起動時自己試験 |
| `tools/hl_check.lpr` | フォームを開かず、同じ自己試験だけを実行する |

```mermaid
classDiagram
  class TMainForm {
    +接続と切断()
    +GPIO DAC PWM を送信()
    +CAN を送信()
    +50ms ポーリング()
  }
  class TComPort {
    +Open()
    +WriteBytes()
    +ReadBytes()
  }
  class THlParser {
    +Push(byte) bool
    +Board
    +Payload
  }
  class THlEnvelope {
    +Sequence
    +Command
    +Snapshot
    +Ack
    +CanLog
  }
  TMainForm --> TComPort : 業務シリアル
  TMainForm --> THlParser : 1 バイトずつ受信
  TMainForm --> THlEnvelope : 符号化して送信
```

フォームは設計時 PPI で配置する。`Application.Scaled` は有効、Windows マニフェストは Per-Monitor V2。ステータスバー幅は実行時に 96 PPI の定数から再計算する。

### 3.2 ファームウェアの共通構造

各基板にアプリケーションタスクが一つあり、入ったあと戻らない。タスク内で先に周辺機能を起動し、そのあとループする。

```mermaid
flowchart TB
  subgraph task [アプリケーションタスク board_app_run]
    init[起動: クロック ピン UART CAN SPI I2C LIN ADC DAC PWM USB Ethernet]
    loop[ループ]
    pollUart[約 20 ms ごとに業務 UART を読む]
    pollIo[25 回ごと約 500 ms でサンプリングと周辺デモ]
    init --> loop
    loop --> pollUart
    loop --> pollIo
  end

  subgraph link [host_link.c]
    parser[HL1 状態機械]
    pb[proto3 Envelope]
    dispatch[コマンド分配]
    parser --> dispatch
    pb --> dispatch
  end

  subgraph chip [チップとベンダパッケージ]
    rtos[FreeRTOS]
    hal[RX: Smart Configurator BSP / RA: FSP]
  end

  pollUart --> parser
  dispatch --> pollIo
  task --> rtos
  init --> hal
```

| 基板 | タスク入口 | 生成箇所 | アプリケーション本体 |
|---|---|---|---|
| RX71M | `main_task` | `freertos_user_port.c`。スタック深さ 512、優先度 3 | `POC_RX71M.c` が `board_app_run()` を呼ぶ |
| RA8P | `new_thread0` | `ra_gen/new_thread0.c`。スタック 4096 バイト | `new_thread0_entry.cpp` が `board_app_run()` を呼ぶ |

`board_resources.cpp` が周辺操作とプロトコルコールバックの両方を持つ。コールバックは、業務 UART への書き込み、GPIO、DAC、PWM の設定、CAN 送信、Snapshot の記入である。

500 ms 周期は、未接続のあいだ GPIO と DAC を書き換える。オシロスコープで観察するためである。正当な要求を最初に受けたあと、この三つは上書きしない。ホストが書いた値を消さないためである。SPI 交換、I2C のアドレス `0x50` への書き込み、LIN Break と `0x55`、自発 CAN は続ける。

### 3.3 二つの基板のソフトウェア差分

フレーム、コマンド番号、フィールド番号は同じである。違いは基板番号、CAN の能力、ベンダパッケージである。

| 項目 | RX71M | RA8P |
|---|---|---|
| プロジェクト | `POC_RX71M` | `POC_RA8P` |
| 基板番号 / 名前 | 1 / `RX71M` | 2 / `RA8P1` |
| 業務 UART | SCI2 | SCI7 |
| ログ UART | SCI1 | SCI8 |
| CAN | クラシック CAN。`fd=true` は拒否 | CAN FD。`fd=true` のとき FD 形式ビットを立てる |
| 送信フレームの CAN FD フラグ | 立てない | 立てる |
| PWM | MTU3 / MTU4、1 kHz、チャネル 0–3 | GPT1 / GPT12 / GPT10、1 kHz、チャネル 0–3 |
| ベンダ層 | `smc_gen`、RX GCC の FreeRTOS 移植 | `ra/fsp`、`ra_gen`、FSP の FreeRTOS 移植 |

PWM チャネル番号はホストの選択と一致する。0、1、2、3。デューティはパーミルで、0–1000。

## 4. 接続とコマンドの時系列

ポートを開いた直後に PING を送る。その後は 50 ms ごとに読む。約 1 秒ごとに、応答がなければ PING を再送し、応答があれば SNAPSHOT に切り替える。基板は 20 ms ごとに業務 UART のバイトをパーサへ渡す。

```mermaid
sequenceDiagram
  participant PC as PC ホスト
  participant MCU as 基板アプリケーションタスク
  PC->>MCU: HL1 PING
  MCU-->>PC: HL1 Snapshot
  Note over MCU: 接続済みにする
  loop 約 1 秒ごと
    PC->>MCU: SNAPSHOT
    MCU-->>PC: Snapshot
  end
  loop 約 500 ms ごと、接続後のみ
    MCU-->>PC: 自発 Snapshot
    MCU-->>PC: 自発 CAN の CanLog
  end
```

設定ボタンはフレームを三つ連続して送る。一フレームにコマンドは一つ。CAN 送信が成功したときは、先に Ack を返し、続けて別フレームで CanLog を送る。

```mermaid
sequenceDiagram
  participant PC as PC ホスト
  participant MCU as 基板
  PC->>MCU: SET_OUTPUTS
  MCU-->>PC: Ack
  PC->>MCU: SET_DAC
  MCU-->>PC: Ack
  PC->>MCU: SET_PWM
  MCU-->>PC: Ack
  PC->>MCU: SEND_CAN
  alt RX71M かつ fd が true
    MCU-->>PC: Ack can fd unsupported
  else メールボックスが送信できる
    MCU-->>PC: Ack 成功
    MCU-->>PC: CanLog
  else メールボックス使用中または引数不正
    MCU-->>PC: Ack bad can または can busy
  end
```

シーケンスは二つに分ける。PC は 1 から増加し、0 は使わない。基板の応答は要求の sequence を返す。基板の自発通知は `0x80000000` から増加する。フレームヘッダには下位 8 ビットだけを入れ、完全な番号は protobuf に置く。

## 5. 周辺機能のソフトウェア上の位置

これらのモジュールはすべて `board_resources.cpp` がレジスタを直接操作する。独立したドライバライブラリは挟まない。

| ソフトウェアモジュール | 現在の動作 |
|---|---|
| 業務 UART | HL1 フレーム。RX は SCI2、RA は SCI7 |
| ログ UART | テキスト行。tick、AD 二系統、入力マスク |
| GPIO | 入力 4 本、出力 4 本。出力はホストがマスクで書き換える |
| ADC | 二系統をサンプリングし、Snapshot に入れる |
| DAC | 12 ビット。ホストが書き換えられる |
| PWM | 4 チャネル、1 kHz。デューティはパーミルで書き換える |
| CAN | 二系統の送信。ホスト要求と、500 ms の自発クラシックフレーム |
| SPI / I2C / LIN | 500 ms ごとに各一回のデモ。ホストコマンドはない |
| USB | デバイスクロックを入れ、D+ を引き上げる。USB-CDC クラスドライバはない |
| Ethernet | ピンとモジュールリセット。RX は MAC を書き、送受信イネーブルも立てる。パケットも TCP/IP もない |

CAN の通知は、送り終わったフレームを直ちに CanLog で出す。受信メールボックスも、RAM のリングログもない。

## 6. ビルド

| 成果物 | ツール | 結果 |
|---|---|---|
| `PC_Host/pc_host.exe` | `lazbuild`、Lazarus 4.8、FPC 3.2.2 | Win64 のフォームプログラム |
| `PC_Host/tools/hl_check.exe` | 同じ FPC。`uHostLink.pas` のみリンク | `ok` または `FAIL` を表示 |
| `POC_RX71M` Debug / Release | e2 studio の GNU make、RX GCC | `.elf` と `.mot` |
| `POC_RA8P` Debug / Release | e2 studio の GNU make、Arm LLVM | `.elf` と `.srec` |
| `protocol/selftest.c` | ホストの C コンパイラ | C の符号化と基板側分配を照合 |

`protocol/host_link.proto` は protoc 36.2 で基準バイト列を作る。ファームウェアのリンクには protoc を入れない。

## 7. 要求図との関係

`embedded_system_requirements_ja.md` の目標構成には、USB、CANlog の RAM バッファ、CAN プロトコルスタック、Ethernet 通信が含まれる。現在のコードとの対応は次のとおりである。

```mermaid
flowchart LR
  subgraph now [現在のソフトウェア]
    uart[業務 UART HL1]
    canTx[CAN 送信と即時 CanLog]
    io[GPIO ADC DAC PWM]
    demo[SPI I2C LIN の周期デモ]
    pin[USB と Ethernet のピンおよびモジュール起動]
  end

  subgraph later [要求図のうちプロトコルスタックになっていない部分]
    cdc[USB-CDC]
    ring[CANlog の RAM バッファと受信]
    tcp[TCP/IP]
  end

  uart -.-> cdc
  canTx -.-> ring
  pin -.-> tcp
```

PC から業務 UART で、二つの基板の GPIO、DAC、PWM、CAN 送信を操作し、状態と送信記録を受け取れる。USB デバイスクラス、Ethernet パケット、CAN 受信ログは、モジュールを開いた層で止まっている。

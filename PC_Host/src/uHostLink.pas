unit uHostLink;

{
  HL1 私有帧 + Protobuf 36.2 proto3 载荷。
  字节布局与 protocol/host_link.c 以及 protoc 36.2 的编码一致。
}

{$mode objfpc}{$H+}
{$modeswitch advancedrecords} { TWr、TRd 里带方法，需要这个开关。 }

interface

uses
  SysUtils;

const
  HL_MAGIC0 = $A5;             { 帧头第一字节。 }
  HL_MAGIC1 = $5A;             { 帧头第二字节。 }
  HL_VERSION = 1;

  HL_BOARD_ANY = 0;            { 不限定板卡。 }
  HL_BOARD_RX71M = 1;
  HL_BOARD_RA8P = 2;

  HL_FLAG_FROM_BOARD = $01;    { bit0：帧来自板卡。主机发出时为 0。 }
  HL_FLAG_CAN_FD = $02;        { bit1：该板支持 CAN FD。 }

  HL_CMD_UNSPECIFIED = 0;
  HL_CMD_PING = 1;             { 探活，板卡回 Snapshot。 }
  HL_CMD_SNAPSHOT = 2;         { 读状态。 }
  HL_CMD_SET_OUTPUTS = 3;
  HL_CMD_SET_DAC = 4;
  HL_CMD_SET_PWM = 5;
  HL_CMD_SEND_CAN = 6;

  HL_ACK_OK = 0;
  HL_ACK_UNSUPPORTED = 1;
  HL_ACK_BAD_ARG = 2;

  HL_CAN_MAX = 8;              { 本协议只收 1–8 字节，不做 CAN FD 长数据。 }
  HL_MAX_PAYLOAD = 192;

type
  THlEnvelope = record
    Sequence: Cardinal;        { 主机从 1 递增。 }
    Command: Cardinal;
    HasSnapshot: Boolean;
    Tick: Cardinal;
    Adc0: Cardinal;
    Adc1: Cardinal;
    Inputs: Cardinal;          { GPIO 输入，低 4 位。 }
    Outputs: Cardinal;
    Dac: Cardinal;
    Board: Cardinal;           { 1 RX71M，2 RA8P。 }
    CanFd: Boolean;            { 该板是否支持 CAN FD。 }
    Name: string;
    HasSetOutputs: Boolean;
    OutputsMask: Cardinal;     { 0–15，0 也要编码。 }
    HasSetDac: Boolean;
    DacCode: Cardinal;         { 0–4095。 }
    HasSetPwm: Boolean;
    PwmChannel: Cardinal;      { 0–3。 }
    PwmDuty: Cardinal;         { 千分比 0–1000。 }
    HasSendCan: Boolean;
    CanChannel: Cardinal;      { 0 或 1。 }
    CanId: Cardinal;           { 11 位标准帧。 }
    CanData: array[0..HL_CAN_MAX - 1] of Byte;
    CanDlc: Byte;
    CanReqFd: Boolean;
    HasCanLog: Boolean;
    LogChannel: Cardinal;
    LogId: Cardinal;
    LogData: array[0..HL_CAN_MAX - 1] of Byte;
    LogDlc: Byte;
    LogFd: Boolean;
    LogTx: Boolean;            { 真表示板卡发出的帧。 }
    HasAck: Boolean;
    AckCommand: Cardinal;
    AckStatus: Cardinal;
    AckDetail: string;         { ASCII 原因，成功时为空。 }
  end;

  THlParser = class
  public
    Board: Byte;               { 最近一帧的目标板卡。 }
    Flags: Byte;
    Seq: Byte;                 { 序号低 8 位。 }
    Len: Word;
    Payload: array[0..HL_MAX_PAYLOAD - 1] of Byte;
    DropReason: string;        { 最近一次丢掉的原因。空表示没有。 }
    constructor Create;
    procedure Reset;
    function Push(Value: Byte): Boolean; { 凑齐且 CRC 正确时返回 True。 }
  private
    FState: Integer;           { 0 在等 0xA5。 }
    FGot: Word;
    FCrcLo: Byte;
    FPrefix: array[0..5] of Byte; { 版本到长度，供重算 CRC。 }
  end;

function HlEncodeEnvelope(const Msg: THlEnvelope): TBytes;
function HlDecodeEnvelope(const Data: TBytes; out Msg: THlEnvelope): Boolean;
function HlEncodeFrame(Board, Flags, Seq: Byte; const Payload: TBytes): TBytes;
function HostLinkMatchesProtoc: Boolean; { 和 protoc 36.2 黄金字节对照。 }
function HlBoardName(Board: Cardinal): string;

implementation

type
  { 往动态数组里写 protobuf。N 是已写字节数。 }
  TWr = record
    B: TBytes;
    N: Integer;
    procedure U8(V: Byte);
    procedure Varint(V: Cardinal);
    procedure Key(Field, Wire: Cardinal);
    procedure U32(Field, V: Cardinal; Force: Boolean);
    procedure BoolField(Field: Cardinal; V, Force: Boolean);
    procedure Raw(Field: Cardinal; const Data: array of Byte; Len: Integer);
    procedure Text(Field: Cardinal; const S: string);
    procedure Sub(Field: Cardinal; const Body: TBytes);
    function Done: TBytes;
  end;

  { 从动态数组里读。I 是当前下标。 }
  TRd = record
    P: TBytes;
    I: Integer;
    function Varint(out V: Cardinal): Boolean;
    function Skip(Wire: Cardinal): Boolean;
    function Exact(var Dst: array of Byte; MaxLen: Integer; out N: Integer): Boolean;
    function Text(out S: string; MaxChars: Integer): Boolean;
    function Sub(out Child: TRd): Boolean;
  end;

{ 清记录。字符串先写成空，避免 FillChar 碰到已有的托管字符串。 }
procedure ClearEnv(out Msg: THlEnvelope);
begin
  FillChar(Msg, SizeOf(Msg), 0);
  Msg.Name := '';
  Msg.AckDetail := '';
end;

procedure TWr.U8(V: Byte);
begin
  if N >= Length(B) then
    SetLength(B, N + 32);     { 一次多留 32 字节，减少反复分配。 }
  B[N] := V;
  Inc(N);
end;

{ 无符号 varint。低 7 位是数据，最高位为 1 表示后面还有。 }
procedure TWr.Varint(V: Cardinal);
var
  Part: Byte;
begin
  repeat
    Part := V and $7F;
    V := V shr 7;
    if V <> 0 then
      Part := Part or $80;
    U8(Part);
  until V = 0;
end;

{ 字段标签：字段号左移 3 位，低 3 位是线型。 }
procedure TWr.Key(Field, Wire: Cardinal);
begin
  Varint((Field shl 3) or Wire);
end;

{ Force 为假时，0 不编码，与 proto3 省略默认值一致。 }
procedure TWr.U32(Field, V: Cardinal; Force: Boolean);
begin
  if (V = 0) and not Force then
    Exit;
  Key(Field, 0);              { 线型 0 = varint。 }
  Varint(V);
end;

{ bool 在线上是 0 或 1。Force 为真时 false 也写出。 }
procedure TWr.BoolField(Field: Cardinal; V, Force: Boolean);
begin
  if (not V) and (not Force) then
    Exit;
  Key(Field, 0);
  if V then
    U8(1)
  else
    U8(0);
end;

{ bytes。空内容不编码。线型 2，先写长度。 }
procedure TWr.Raw(Field: Cardinal; const Data: array of Byte; Len: Integer);
var
  K: Integer;
begin
  if Len <= 0 then
    Exit;
  Key(Field, 2);
  Varint(Cardinal(Len));
  for K := 0 to Len - 1 do
    U8(Data[K]);
end;

{ 字符串按 bytes 编码，不含结尾 0。 }
procedure TWr.Text(Field: Cardinal; const S: string);
var
  RawBytes: array of Byte;
  K: Integer;
begin
  if S = '' then
    Exit;
  SetLength(RawBytes, Length(S));
  for K := 1 to Length(S) do
    RawBytes[K - 1] := Byte(Ord(S[K])); { 按单字节 ASCII 写出。 }
  if Length(RawBytes) > 0 then
    Raw(Field, RawBytes, Length(RawBytes));
end;

{ 嵌套消息。长度可以为 0。 }
procedure TWr.Sub(Field: Cardinal; const Body: TBytes);
begin
  Key(Field, 2);
  Varint(Cardinal(Length(Body)));
  if Length(Body) > 0 then
    Raw(0, Body, 0);          { 长度参数是 0，这一调用不会写出任何字节。 }
  if Length(Body) > 0 then
  begin
    if N + Length(Body) > Length(B) then
      SetLength(B, N + Length(Body) + 16);
    Move(Body[0], B[N], Length(Body)); { 真正把子消息拷进来。 }
    Inc(N, Length(Body));
  end;
end;

function TWr.Done: TBytes;
begin
  SetLength(B, N);            { 丢掉多留的尾部。 }
  Result := B;
end;

{ 读 uint32 varint。超过 5 字节或数据被截断则失败。 }
function TRd.Varint(out V: Cardinal): Boolean;
var
  Shift: Integer;
  Part: Byte;
begin
  V := 0;
  Shift := 0;
  Result := False;
  while Shift <= 28 do        { 32 位最多 5 组 7 位。 }
  begin
    if I >= Length(P) then
      Exit;
    Part := P[I];
    Inc(I);
    V := V or (Cardinal(Part and $7F) shl Shift);
    if (Part and $80) = 0 then
      Exit(True);             { 最高位为 0，本 varint 结束。 }
    Inc(Shift, 7);
  end;
end;

{ 跳过不认识的字段，避免以后加字段时解不开。 }
function TRd.Skip(Wire: Cardinal): Boolean;
var
  N: Cardinal;
begin
  Result := False;
  N := 0;
  if Wire = 0 then
    Exit(Varint(N));          { varint，读掉即可。 }
  if Wire = 1 then
    N := 8                    { 64 位定长。 }
  else if Wire = 5 then
    N := 4                    { 32 位定长。 }
  else if Wire = 2 then
  begin
    if not Varint(N) then     { 长度定界，先读长度。 }
      Exit;
  end
  else
    Exit;                     { 线型 3、4 是组，proto3 不用。 }
  if (I > Length(P)) or (Cardinal(Length(P) - I) < N) then
    Exit;
  Inc(I, Integer(N));
  Result := True;
end;

{ 读定长字节。超过 MaxLen 时仍把输入吃掉，但返回失败。 }
function TRd.Exact(var Dst: array of Byte; MaxLen: Integer; out N: Integer): Boolean;
var
  Count: Cardinal;
begin
  Result := False;
  N := 0;
  if not Varint(Count) then
    Exit;
  if (I > Length(P)) or (Cardinal(Length(P) - I) < Count) or (Count > Cardinal(MaxLen)) then
  begin
    if (I <= Length(P)) and (Cardinal(Length(P) - I) >= Count) then
      Inc(I, Integer(Count)); { CAN 超过 8 字节，整段丢掉。 }
    Exit;
  end;
  if Count > 0 then
    Move(P[I], Dst[0], Count);
  Inc(I, Integer(Count));
  N := Integer(Count);
  Result := True;
end;

{ 读字符串并截断到 MaxChars。游标按原始长度前进。 }
function TRd.Text(out S: string; MaxChars: Integer): Boolean;
var
  Count: Cardinal;
  CopyN, K: Integer;
begin
  Result := False;
  S := '';
  if not Varint(Count) then
    Exit;
  if (I > Length(P)) or (Cardinal(Length(P) - I) < Count) then
    Exit;
  CopyN := Integer(Count);
  if CopyN > MaxChars then
    CopyN := MaxChars;
  SetLength(S, CopyN);
  for K := 0 to CopyN - 1 do
    S[K + 1] := Char(P[I + K]); { FPC 字符串从 1 起。 }
  Inc(I, Integer(Count));
  Result := True;
end;

{ 切出嵌套消息。子游标从 0 开始，外层跳过这段。 }
function TRd.Sub(out Child: TRd): Boolean;
var
  Count: Cardinal;
begin
  Result := False;
  if not Varint(Count) then
    Exit;
  if (I > Length(P)) or (Cardinal(Length(P) - I) < Count) then
    Exit;
  SetLength(Child.P, Count);
  if Count > 0 then
    Move(P[I], Child.P[0], Count);
  Child.I := 0;
  Inc(I, Integer(Count));
  Result := True;
end;

{ Snapshot。数值为 0 或空名字时省略。 }
function EncodeSnapshot(const Msg: THlEnvelope): TBytes;
var
  W: TWr;
begin
  FillChar(W, SizeOf(W), 0);  { 动态数组还是空的，可以整块清零。 }
  W.U32(1, Msg.Tick, False);
  W.U32(2, Msg.Adc0, False);
  W.U32(3, Msg.Adc1, False);
  W.U32(4, Msg.Inputs, False);
  W.U32(5, Msg.Outputs, False);
  W.U32(6, Msg.Dac, False);
  W.U32(7, Msg.Board, False);
  W.BoolField(8, Msg.CanFd, False); { RX 为假，这一位省略。 }
  W.Text(9, Msg.Name);
  Result := W.Done;
end;

{ SendCan 与 CanLog 字段号相同。Log 为真时多写 tx。通道、ID、fd 即使是 0 也写出。 }
function EncodeCan(const Msg: THlEnvelope; Log: Boolean): TBytes;
var
  W: TWr;
  Channel, Id: Cardinal;
  Data: array[0..HL_CAN_MAX - 1] of Byte;
  Dlc: Integer;
  Fd, Tx: Boolean;
begin
  FillChar(W, SizeOf(W), 0);
  if Log then
  begin
    Channel := Msg.LogChannel;
    Id := Msg.LogId;
    Data := Msg.LogData;
    Dlc := Msg.LogDlc;
    Fd := Msg.LogFd;
    Tx := Msg.LogTx;
  end
  else
  begin
    Channel := Msg.CanChannel;
    Id := Msg.CanId;
    Data := Msg.CanData;
    Dlc := Msg.CanDlc;
    Fd := Msg.CanReqFd;
    Tx := False;
  end;
  W.U32(1, Channel, True);    { Force，通道 0 也要在线上。 }
  W.U32(2, Id, True);
  W.Raw(3, Data, Dlc);
  W.BoolField(4, Fd, True);   { 经典 CAN 也写出 false。 }
  if Log then
    W.BoolField(5, Tx, True); { 只有 CanLog 有 tx。 }
  Result := W.Done;
end;

{ 编码整包 Envelope。出现哪个 has_*，就嵌进对应字段。 }
function HlEncodeEnvelope(const Msg: THlEnvelope): TBytes;
var
  W: TWr;
  Nested: TBytes;
  Inner: TWr;
begin
  FillChar(W, SizeOf(W), 0);
  W.U32(1, Msg.Sequence, False);
  W.U32(2, Msg.Command, False);
  if Msg.HasSnapshot then
    W.Sub(3, EncodeSnapshot(Msg));
  if Msg.HasSetOutputs then
  begin
    FillChar(Inner, SizeOf(Inner), 0);
    Inner.U32(1, Msg.OutputsMask, True); { 掩码 0 也编码。 }
    W.Sub(4, Inner.Done);
  end;
  if Msg.HasSetDac then
  begin
    FillChar(Inner, SizeOf(Inner), 0);
    Inner.U32(1, Msg.DacCode, True);
    W.Sub(5, Inner.Done);
  end;
  if Msg.HasSetPwm then
  begin
    FillChar(Inner, SizeOf(Inner), 0);
    Inner.U32(1, Msg.PwmChannel, True);
    Inner.U32(2, Msg.PwmDuty, True);     { 通道 0、占空比 0 都保留。 }
    W.Sub(6, Inner.Done);
  end;
  if Msg.HasSendCan then
    W.Sub(7, EncodeCan(Msg, False));
  if Msg.HasCanLog then
    W.Sub(8, EncodeCan(Msg, True));
  if Msg.HasAck then
  begin
    FillChar(Inner, SizeOf(Inner), 0);
    Inner.U32(1, Msg.AckCommand, False);
    Inner.U32(2, Msg.AckStatus, False);  { 成功时状态 0 省略。 }
    Inner.Text(3, Msg.AckDetail);
    Nested := Inner.Done;
    W.Sub(9, Nested);
  end;
  Result := W.Done;
end;

{ 解 Snapshot。字段 9 是名字，其余是 varint。 }
function DecodeSnapshot(var R: TRd; var Msg: THlEnvelope): Boolean;
var
  Tag, Field, Wire, V: Cardinal;
begin
  Result := False;
  while R.I < Length(R.P) do
  begin
    if not R.Varint(Tag) then
      Exit;
    Field := Tag shr 3;        { 字段号。 }
    Wire := Tag and 7;         { 线型。 }
    if (Field = 9) and (Wire = 2) then
    begin
      if not R.Text(Msg.Name, 32) then
        Exit;
      Continue;
    end;
    if Wire <> 0 then
    begin
      if not R.Skip(Wire) then
        Exit;
      Continue;
    end;
    if not R.Varint(V) then
      Exit;
    case Field of
      1: Msg.Tick := V;
      2: Msg.Adc0 := V;
      3: Msg.Adc1 := V;
      4: Msg.Inputs := V;
      5: Msg.Outputs := V;
      6: Msg.Dac := V;
      7: Msg.Board := V;
      8: Msg.CanFd := V <> 0;
    end;
  end;
  Result := True;
end;

{ 解 SendCan 或 CanLog。Log 为真时写入 Log* 字段。 }
function DecodeCan(var R: TRd; var Msg: THlEnvelope; Log: Boolean): Boolean;
var
  Tag, Field, Wire, V: Cardinal;
  Tmp: array[0..HL_CAN_MAX - 1] of Byte;
  N: Integer;
begin
  Result := False;
  while R.I < Length(R.P) do
  begin
    if not R.Varint(Tag) then
      Exit;
    Field := Tag shr 3;
    Wire := Tag and 7;
    if (Field = 3) and (Wire = 2) then { 数据字节。 }
    begin
      FillChar(Tmp, SizeOf(Tmp), 0);
      if not R.Exact(Tmp, HL_CAN_MAX, N) then
        Exit;
      if Log then
      begin
        Move(Tmp, Msg.LogData, N);
        Msg.LogDlc := Byte(N);
      end
      else
      begin
        Move(Tmp, Msg.CanData, N);
        Msg.CanDlc := Byte(N);
      end;
      Continue;
    end;
    if Wire <> 0 then
    begin
      if not R.Skip(Wire) then
        Exit;
      Continue;
    end;
    if not R.Varint(V) then
      Exit;
    if Log then
      case Field of
        1: Msg.LogChannel := V;
        2: Msg.LogId := V;
        4: Msg.LogFd := V <> 0;
        5: Msg.LogTx := V <> 0;
      end
    else
      case Field of
        1: Msg.CanChannel := V;
        2: Msg.CanId := V;
        4: Msg.CanReqFd := V <> 0;
      end;
  end;
  Result := True;
end;

{ 解 Ack。字段 3 是原因字符串。 }
function DecodeAck(var R: TRd; var Msg: THlEnvelope): Boolean;
var
  Tag, Field, Wire, V: Cardinal;
begin
  Result := False;
  while R.I < Length(R.P) do
  begin
    if not R.Varint(Tag) then
      Exit;
    Field := Tag shr 3;
    Wire := Tag and 7;
    if (Field = 3) and (Wire = 2) then
    begin
      if not R.Text(Msg.AckDetail, 64) then
        Exit;
      Continue;
    end;
    if Wire <> 0 then
    begin
      if not R.Skip(Wire) then
        Exit;
      Continue;
    end;
    if not R.Varint(V) then
      Exit;
    if Field = 1 then
      Msg.AckCommand := V
    else if Field = 2 then
      Msg.AckStatus := V;
  end;
  Result := True;
end;

{ 解只有一到两个 varint 的子消息。缺省为 0。 }
function DecodeTwo(var R: TRd; out A, B: Cardinal): Boolean;
var
  Tag, Field, Wire, V: Cardinal;
begin
  Result := False;
  A := 0;                     { 字段 1。 }
  B := 0;                     { 字段 2。SetOutputs、SetDac 不用它。 }
  while R.I < Length(R.P) do
  begin
    if not R.Varint(Tag) then
      Exit;
    Field := Tag shr 3;
    Wire := Tag and 7;
    if Wire <> 0 then
    begin
      if not R.Skip(Wire) then
        Exit;
      Continue;
    end;
    if not R.Varint(V) then
      Exit;
    if Field = 1 then
      A := V
    else if Field = 2 then
      B := V;
  end;
  Result := True;
end;

{ 解 Envelope。成功返回 True。字段 3–9 是嵌套消息。 }
function HlDecodeEnvelope(const Data: TBytes; out Msg: THlEnvelope): Boolean;
var
  R, Sub: TRd;
  Tag, Field, Wire, V, Ignore: Cardinal;
begin
  Result := False;
  ClearEnv(Msg);
  R.P := Data;
  R.I := 0;
  while R.I < Length(R.P) do
  begin
    if not R.Varint(Tag) then
      Exit;
    Field := Tag shr 3;
    Wire := Tag and 7;
    if (Wire = 2) and (Field >= 3) and (Field <= 9) then
    begin
      if not R.Sub(Sub) then
        Exit;
      case Field of
        3:
          begin
            Msg.HasSnapshot := True;
            if not DecodeSnapshot(Sub, Msg) then
              Exit;
          end;
        4:
          begin
            Msg.HasSetOutputs := True;
            if not DecodeTwo(Sub, Msg.OutputsMask, Ignore) then
              Exit;
          end;
        5:
          begin
            Msg.HasSetDac := True;
            if not DecodeTwo(Sub, Msg.DacCode, Ignore) then
              Exit;
          end;
        6:
          begin
            Msg.HasSetPwm := True;
            if not DecodeTwo(Sub, Msg.PwmChannel, Msg.PwmDuty) then
              Exit;
          end;
        7:
          begin
            Msg.HasSendCan := True;
            if not DecodeCan(Sub, Msg, False) then
              Exit;
          end;
        8:
          begin
            Msg.HasCanLog := True;
            if not DecodeCan(Sub, Msg, True) then
              Exit;
          end;
        9:
          begin
            Msg.HasAck := True;
            if not DecodeAck(Sub, Msg) then
              Exit;
          end;
      end;
      Continue;
    end;
    if Wire <> 0 then
    begin
      if not R.Skip(Wire) then
        Exit;
      Continue;
    end;
    if not R.Varint(V) then
      Exit;
    if Field = 1 then
      Msg.Sequence := V
    else if Field = 2 then
      Msg.Command := V;
  end;
  Result := True;
end;

{ CRC-16/CCITT-FALSE：多项式 0x1021，初值由调用方传入，不反射，结果不异或。 }
function Crc16(const Data: array of Byte; Len: Integer; Seed: Word): Word;
var
  I, B: Integer;
  C: Word;
begin
  C := Seed;
  for I := 0 to Len - 1 do
  begin
    C := C xor (Word(Data[I]) shl 8);
    for B := 0 to 7 do
      if (C and $8000) <> 0 then
        C := Word((C shl 1) xor $1021)
      else
        C := Word(C shl 1);
  end;
  Result := C;
end;

{ 组 HL1 帧：A5 5A | 版本 | 板卡 | 标志 | 序号 | 长度小端 | 载荷 | CRC 小端。 }
function HlEncodeFrame(Board, Flags, Seq: Byte; const Payload: TBytes): TBytes;
var
  Prefix: array[0..5] of Byte;
  Crc: Word;
  N, I: Integer;
begin
  Prefix[0] := HL_VERSION;
  Prefix[1] := Board;
  Prefix[2] := Flags;
  Prefix[3] := Seq;                          { 只放序号低 8 位。 }
  Prefix[4] := Byte(Length(Payload) and $FF);
  Prefix[5] := Byte((Length(Payload) shr 8) and $FF);
  Result := nil;                             { 托管结果先清空，避免编译器提示。 }
  Crc := Crc16(Prefix, 6, $FFFF);            { CRC 从版本算起，不含魔数。 }
  if Length(Payload) > 0 then
    Crc := Crc16(Payload, Length(Payload), Crc);
  SetLength(Result, 10 + Length(Payload));   { 2 魔数 + 6 头 + 载荷 + 2 CRC。 }
  Result[0] := HL_MAGIC0;
  Result[1] := HL_MAGIC1;
  for I := 0 to 5 do
    Result[2 + I] := Prefix[I];
  N := 8;
  for I := 0 to Length(Payload) - 1 do
  begin
    Result[N] := Payload[I];
    Inc(N);
  end;
  Result[N] := Byte(Crc and $FF);            { CRC 低字节在前。 }
  Result[N + 1] := Byte((Crc shr 8) and $FF);
end;

constructor THlParser.Create;
begin
  inherited Create;
  Reset;
end;

procedure THlParser.Reset;
begin
  FState := 0;            { 回到等 0xA5。 }
  FGot := 0;
  Board := 0;
  Flags := 0;
  Seq := 0;
  Len := 0;
end;

{ 喂入一个字节。状态 0–10 对应魔数、版本、板卡、标志、序号、长度、载荷、CRC。 }
function THlParser.Push(Value: Byte): Boolean;
var
  Crc, Got: Word;
begin
  Result := False;
  case FState of
    0:
      if Value = HL_MAGIC0 then
        FState := 1;
    1:
      if Value = HL_MAGIC1 then
        FState := 2
      else if Value = HL_MAGIC0 then
        FState := 1             { 连续两个 0xA5，后一个仍可当帧头。 }
      else
        FState := 0;
    2:
      begin
        if Value <> HL_VERSION then
        begin
          DropReason := Format('帧版本 %u，不是 %u。', [Value, HL_VERSION]);
          FState := 0;
          if Value = HL_MAGIC0 then
            FState := 1;        { 版本不对，这个字节若是 0xA5 就重新同步。 }
          Exit;
        end;
        FPrefix[0] := Value;
        FState := 3;
      end;
    3:
      begin
        Board := Value;
        FPrefix[1] := Value;
        FState := 4;
      end;
    4:
      begin
        Flags := Value;
        FPrefix[2] := Value;
        FState := 5;
      end;
    5:
      begin
        Seq := Value;
        FPrefix[3] := Value;
        FState := 6;
      end;
    6:
      begin
        Len := Value;           { 长度低字节。 }
        FPrefix[4] := Value;
        FState := 7;
      end;
    7:
      begin
        Len := Len or (Word(Value) shl 8); { 长度高字节，小端。 }
        FPrefix[5] := Value;
        FGot := 0;
        if Len > HL_MAX_PAYLOAD then
        begin
          DropReason := Format('载荷 %u 字节，超过 %u。', [Len, HL_MAX_PAYLOAD]);
          FState := 0;
          if Value = HL_MAGIC0 then
            FState := 1;
          Exit;
        end;
        if Len = 0 then
          FState := 9           { 空载荷直接等 CRC。 }
        else
          FState := 8;
      end;
    8:
      begin
        Payload[FGot] := Value;
        Inc(FGot);
        if FGot >= Len then
          FState := 9;
      end;
    9:
      begin
        FCrcLo := Value;
        FState := 10;
      end;
    10:
      begin
        Got := FCrcLo or (Word(Value) shl 8);
        Crc := Crc16(FPrefix, 6, $FFFF);
        if Len > 0 then
          Crc := Crc16(Payload, Len, Crc);
        FState := 0;            { 无论对错都回到找下一帧。 }
        FGot := 0;
        Result := Got = Crc;
        if not Result then
          DropReason := Format('CRC 期望 $%04x，收到 $%04x，载荷 %u 字节。', [Crc, Got, Len]);
      end;
  else
    FState := 0;
  end;
end;

function SameBytes(const Got: TBytes; const Exp: array of Byte): Boolean;
var
  I: Integer;
begin
  Result := Length(Got) = Length(Exp);
  if not Result then
    Exit;
  for I := 0 to High(Exp) do
    if Got[I] <> Exp[I] then
      Exit(False);
end;

{ 对照 protoc 36.2：PING、掩码 0 的输出、通道 0 占空比 0 的 PWM，再回环一帧 Snapshot。 }
function HostLinkMatchesProtoc: Boolean;
var
  Msg: THlEnvelope;
  Back: THlEnvelope;
  Frame: TBytes;
  Parser: THlParser;
  I: Integer;
  Payload: TBytes;
const
  Ping: array[0..3] of Byte = ($08, $01, $10, $01);
  Outputs: array[0..7] of Byte = ($08, $02, $10, $03, $22, $02, $08, $00);
  Pwm: array[0..9] of Byte = ($08, $06, $10, $05, $32, $04, $08, $00, $10, $00);
begin
  Result := False;
  ClearEnv(Msg);
  Msg.Sequence := 1;
  Msg.Command := HL_CMD_PING;
  if not SameBytes(HlEncodeEnvelope(Msg), Ping) then
    Exit;
  ClearEnv(Msg);
  Msg.Sequence := 2;
  Msg.Command := HL_CMD_SET_OUTPUTS;
  Msg.HasSetOutputs := True;   { 掩码保持 0，线上仍要有 08 00。 }
  if not SameBytes(HlEncodeEnvelope(Msg), Outputs) then
    Exit;
  ClearEnv(Msg);
  Msg.Sequence := 6;
  Msg.Command := HL_CMD_SET_PWM;
  Msg.HasSetPwm := True;       { 通道 0、占空比 0 都要编码。 }
  if not SameBytes(HlEncodeEnvelope(Msg), Pwm) then
    Exit;
  ClearEnv(Msg);
  Msg.Sequence := 3;
  Msg.Command := HL_CMD_SNAPSHOT;
  Msg.HasSnapshot := True;
  Msg.Tick := 9;
  Msg.Adc0 := 4095;
  Msg.Adc1 := 1;
  Msg.Inputs := 15;
  Msg.Outputs := 6;
  Msg.Dac := 2048;
  Msg.Board := HL_BOARD_RA8P;
  Msg.CanFd := True;
  Msg.Name := 'RA8P1';
  Payload := HlEncodeEnvelope(Msg);
  if not HlDecodeEnvelope(Payload, Back) then
    Exit;
  if (Back.Tick <> 9) or (Back.Adc0 <> 4095) or (Back.Board <> HL_BOARD_RA8P) or
     (Back.Name <> 'RA8P1') or not Back.CanFd then
    Exit;
  Frame := HlEncodeFrame(HL_BOARD_RX71M, HL_FLAG_FROM_BOARD, 3, Payload);
  Parser := THlParser.Create;
  try
    for I := 0 to High(Frame) do
      if Parser.Push(Frame[I]) then
      begin
        if Parser.Board <> HL_BOARD_RX71M then
          Exit;
        SetLength(Payload, Parser.Len);
        if Parser.Len > 0 then
          Move(Parser.Payload[0], Payload[0], Parser.Len);
        if not HlDecodeEnvelope(Payload, Back) then
          Exit;
        if Back.Name <> 'RA8P1' then
          Exit;
        Exit(True);            { 帧能收齐，载荷还能解回名字。 }
      end;
  finally
    Parser.Free;
  end;
end;

function HlBoardName(Board: Cardinal): string;
begin
  case Board of
    HL_BOARD_RX71M: Result := 'RX71M';
    HL_BOARD_RA8P: Result := 'RA8P';
  else
    Result := '未知板';
  end;
end;

function HlCommandName(Command: Cardinal): string;
begin
  case Command of
    HL_CMD_PING: Result := 'PING';
    HL_CMD_SNAPSHOT: Result := 'SNAPSHOT';
    HL_CMD_SET_OUTPUTS: Result := 'SET_OUTPUTS';
    HL_CMD_SET_DAC: Result := 'SET_DAC';
    HL_CMD_SET_PWM: Result := 'SET_PWM';
    HL_CMD_SEND_CAN: Result := 'SEND_CAN';
  else
    Result := Format('CMD_%u', [Command]);
  end;
end;

end.

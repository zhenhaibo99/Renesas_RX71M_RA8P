unit uHostLink;

{
  HL1 私有帧 + Protobuf 36.2 proto3 载荷。
  字节布局与 protocol/host_link.c 以及 protoc 36.2 的编码一致。
}

{$mode objfpc}{$H+}
{$modeswitch advancedrecords}

interface

uses
  SysUtils;

const
  HL_MAGIC0 = $A5;
  HL_MAGIC1 = $5A;
  HL_VERSION = 1;

  HL_BOARD_ANY = 0;
  HL_BOARD_RX71M = 1;
  HL_BOARD_RA8P = 2;

  HL_FLAG_FROM_BOARD = $01;
  HL_FLAG_CAN_FD = $02;

  HL_CMD_UNSPECIFIED = 0;
  HL_CMD_PING = 1;
  HL_CMD_SNAPSHOT = 2;
  HL_CMD_SET_OUTPUTS = 3;
  HL_CMD_SET_DAC = 4;
  HL_CMD_SET_PWM = 5;
  HL_CMD_SEND_CAN = 6;

  HL_ACK_OK = 0;
  HL_ACK_UNSUPPORTED = 1;
  HL_ACK_BAD_ARG = 2;

  HL_CAN_MAX = 8;
  HL_MAX_PAYLOAD = 192;

type
  THlEnvelope = record
    Sequence: Cardinal;
    Command: Cardinal;
    HasSnapshot: Boolean;
    Tick: Cardinal;
    Adc0: Cardinal;
    Adc1: Cardinal;
    Inputs: Cardinal;
    Outputs: Cardinal;
    Dac: Cardinal;
    Board: Cardinal;
    CanFd: Boolean;
    Name: string;
    HasSetOutputs: Boolean;
    OutputsMask: Cardinal;
    HasSetDac: Boolean;
    DacCode: Cardinal;
    HasSetPwm: Boolean;
    PwmChannel: Cardinal;
    PwmDuty: Cardinal;
    HasSendCan: Boolean;
    CanChannel: Cardinal;
    CanId: Cardinal;
    CanData: array[0..HL_CAN_MAX - 1] of Byte;
    CanDlc: Byte;
    CanReqFd: Boolean;
    HasCanLog: Boolean;
    LogChannel: Cardinal;
    LogId: Cardinal;
    LogData: array[0..HL_CAN_MAX - 1] of Byte;
    LogDlc: Byte;
    LogFd: Boolean;
    LogTx: Boolean;
    HasAck: Boolean;
    AckCommand: Cardinal;
    AckStatus: Cardinal;
    AckDetail: string;
  end;

  THlParser = class
  public
    Board: Byte;
    Flags: Byte;
    Seq: Byte;
    Len: Word;
    Payload: array[0..HL_MAX_PAYLOAD - 1] of Byte;
    constructor Create;
    procedure Reset;
    function Push(Value: Byte): Boolean;
  private
    FState: Integer;
    FGot: Word;
    FCrcLo: Byte;
    FPrefix: array[0..5] of Byte;
  end;

function HlEncodeEnvelope(const Msg: THlEnvelope): TBytes;
function HlDecodeEnvelope(const Data: TBytes; out Msg: THlEnvelope): Boolean;
function HlEncodeFrame(Board, Flags, Seq: Byte; const Payload: TBytes): TBytes;
function HostLinkMatchesProtoc: Boolean;
function HlBoardName(Board: Cardinal): string;

implementation

type
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

  TRd = record
    P: TBytes;
    I: Integer;
    function Varint(out V: Cardinal): Boolean;
    function Skip(Wire: Cardinal): Boolean;
    function Exact(var Dst: array of Byte; MaxLen: Integer; out N: Integer): Boolean;
    function Text(out S: string; MaxChars: Integer): Boolean;
    function Sub(out Child: TRd): Boolean;
  end;

procedure ClearEnv(out Msg: THlEnvelope);
begin
  FillChar(Msg, SizeOf(Msg), 0);
  Msg.Name := '';
  Msg.AckDetail := '';
end;

procedure TWr.U8(V: Byte);
begin
  if N >= Length(B) then
    SetLength(B, N + 32);
  B[N] := V;
  Inc(N);
end;

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

procedure TWr.Key(Field, Wire: Cardinal);
begin
  Varint((Field shl 3) or Wire);
end;

procedure TWr.U32(Field, V: Cardinal; Force: Boolean);
begin
  if (V = 0) and not Force then
    Exit;
  Key(Field, 0);
  Varint(V);
end;

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

procedure TWr.Text(Field: Cardinal; const S: string);
var
  RawBytes: array of Byte;
  K: Integer;
begin
  if S = '' then
    Exit;
  SetLength(RawBytes, Length(S));
  for K := 1 to Length(S) do
    RawBytes[K - 1] := Byte(Ord(S[K]));
  if Length(RawBytes) > 0 then
    Raw(Field, RawBytes, Length(RawBytes));
end;

procedure TWr.Sub(Field: Cardinal; const Body: TBytes);
begin
  Key(Field, 2);
  Varint(Cardinal(Length(Body)));
  if Length(Body) > 0 then
    Raw(0, Body, 0);
  if Length(Body) > 0 then
  begin
    if N + Length(Body) > Length(B) then
      SetLength(B, N + Length(Body) + 16);
    Move(Body[0], B[N], Length(Body));
    Inc(N, Length(Body));
  end;
end;

function TWr.Done: TBytes;
begin
  SetLength(B, N);
  Result := B;
end;

function TRd.Varint(out V: Cardinal): Boolean;
var
  Shift: Integer;
  Part: Byte;
begin
  V := 0;
  Shift := 0;
  Result := False;
  while Shift <= 28 do
  begin
    if I >= Length(P) then
      Exit;
    Part := P[I];
    Inc(I);
    V := V or (Cardinal(Part and $7F) shl Shift);
    if (Part and $80) = 0 then
      Exit(True);
    Inc(Shift, 7);
  end;
end;

function TRd.Skip(Wire: Cardinal): Boolean;
var
  N: Cardinal;
begin
  Result := False;
  N := 0;
  if Wire = 0 then
    Exit(Varint(N));
  if Wire = 1 then
    N := 8
  else if Wire = 5 then
    N := 4
  else if Wire = 2 then
  begin
    if not Varint(N) then
      Exit;
  end
  else
    Exit;
  if (I > Length(P)) or (Cardinal(Length(P) - I) < N) then
    Exit;
  Inc(I, Integer(N));
  Result := True;
end;

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
      Inc(I, Integer(Count));
    Exit;
  end;
  if Count > 0 then
    Move(P[I], Dst[0], Count);
  Inc(I, Integer(Count));
  N := Integer(Count);
  Result := True;
end;

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
    S[K + 1] := Char(P[I + K]);
  Inc(I, Integer(Count));
  Result := True;
end;

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

function EncodeSnapshot(const Msg: THlEnvelope): TBytes;
var
  W: TWr;
begin
  FillChar(W, SizeOf(W), 0);
  W.U32(1, Msg.Tick, False);
  W.U32(2, Msg.Adc0, False);
  W.U32(3, Msg.Adc1, False);
  W.U32(4, Msg.Inputs, False);
  W.U32(5, Msg.Outputs, False);
  W.U32(6, Msg.Dac, False);
  W.U32(7, Msg.Board, False);
  W.BoolField(8, Msg.CanFd, False);
  W.Text(9, Msg.Name);
  Result := W.Done;
end;

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
  W.U32(1, Channel, True);
  W.U32(2, Id, True);
  W.Raw(3, Data, Dlc);
  W.BoolField(4, Fd, True);
  if Log then
    W.BoolField(5, Tx, True);
  Result := W.Done;
end;

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
    Inner.U32(1, Msg.OutputsMask, True);
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
    Inner.U32(2, Msg.PwmDuty, True);
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
    Inner.U32(2, Msg.AckStatus, False);
    Inner.Text(3, Msg.AckDetail);
    Nested := Inner.Done;
    W.Sub(9, Nested);
  end;
  Result := W.Done;
end;

function DecodeSnapshot(var R: TRd; var Msg: THlEnvelope): Boolean;
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
    if (Field = 3) and (Wire = 2) then
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

function DecodeTwo(var R: TRd; out A, B: Cardinal): Boolean;
var
  Tag, Field, Wire, V: Cardinal;
begin
  Result := False;
  A := 0;
  B := 0;
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

function HlEncodeFrame(Board, Flags, Seq: Byte; const Payload: TBytes): TBytes;
var
  Prefix: array[0..5] of Byte;
  Crc: Word;
  N, I: Integer;
begin
  Prefix[0] := HL_VERSION;
  Prefix[1] := Board;
  Prefix[2] := Flags;
  Prefix[3] := Seq;
  Prefix[4] := Byte(Length(Payload) and $FF);
  Prefix[5] := Byte((Length(Payload) shr 8) and $FF);
  Result := nil;
  Crc := Crc16(Prefix, 6, $FFFF);
  if Length(Payload) > 0 then
    Crc := Crc16(Payload, Length(Payload), Crc);
  SetLength(Result, 10 + Length(Payload));
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
  Result[N] := Byte(Crc and $FF);
  Result[N + 1] := Byte((Crc shr 8) and $FF);
end;

constructor THlParser.Create;
begin
  inherited Create;
  Reset;
end;

procedure THlParser.Reset;
begin
  FState := 0;
  FGot := 0;
  Board := 0;
  Flags := 0;
  Seq := 0;
  Len := 0;
end;

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
        FState := 1
      else
        FState := 0;
    2:
      begin
        if Value <> HL_VERSION then
        begin
          FState := 0;
          if Value = HL_MAGIC0 then
            FState := 1;
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
        Len := Value;
        FPrefix[4] := Value;
        FState := 7;
      end;
    7:
      begin
        Len := Len or (Word(Value) shl 8);
        FPrefix[5] := Value;
        FGot := 0;
        if Len > HL_MAX_PAYLOAD then
        begin
          FState := 0;
          if Value = HL_MAGIC0 then
            FState := 1;
          Exit;
        end;
        if Len = 0 then
          FState := 9
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
        FState := 0;
        FGot := 0;
        Result := Got = Crc;
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
  Msg.HasSetOutputs := True;
  if not SameBytes(HlEncodeEnvelope(Msg), Outputs) then
    Exit;
  ClearEnv(Msg);
  Msg.Sequence := 6;
  Msg.Command := HL_CMD_SET_PWM;
  Msg.HasSetPwm := True;
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
        Exit(True);
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

end.

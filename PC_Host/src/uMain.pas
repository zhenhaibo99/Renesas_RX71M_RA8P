unit uMain;

{
  主窗体随系统 DPI 缩放。
  左侧通过业务串口收发 HL1 帧，载荷是 Protobuf 36.2。
  RX71M 与 RA8P 用同一帧；CAN FD 只有 RA8P 会执行。
}

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, ExtCtrls,
  ComCtrls, uComPort, uHostLink;

type

  { TMainForm }

  TMainForm = class(TForm)
    pnlTop: TPanel;
    lblTitle: TLabel;
    lblSub: TLabel;
    StatusBar1: TStatusBar;
    pnlSide: TPanel;
    grpLink: TGroupBox;
    lblChannel: TLabel;
    cmbPort: TComboBox;     { 业务 COM 口。 }
    lblBoard: TLabel;
    cmbBoard: TComboBox;    { 自动 / RX71M / RA8P。 }
    pnlBottom: TPanel;
    btnRefresh: TButton;
    btnConnect: TButton;
    scrCmd: TScrollBox;     { 命令区可滚动，150% 下不被裁掉。 }
    pnlCmd: TPanel;
    lblIo: TLabel;
    edtGpio: TEdit;         { 输出掩码 0–15。 }
    edtDac: TEdit;          { DAC 码 0–4095。 }
    lblPwm: TLabel;
    cmbPwm: TComboBox;      { PWM 通道 0–3。 }
    edtDuty: TEdit;         { 占空比千分比 0–1000。 }
    btnApply: TButton;
    lblCan: TLabel;
    cmbCan: TComboBox;      { CAN 通道 0 或 1。 }
    edtCanId: TEdit;        { 11 位标准 ID。 }
    lblData: TLabel;
    edtCanData: TEdit;      { 十六进制数据，最多 8 字节。 }
    chkCanFd: TCheckBox;
    btnCan: TButton;
    btnRead: TButton;
    lblNote: TLabel;
    splMain: TSplitter;
    memLog: TMemo;
    procedure FormShow(Sender: TObject);
    procedure FormDestroy(Sender: TObject);
    procedure btnRefreshClick(Sender: TObject);
    procedure btnConnectClick(Sender: TObject);
    procedure btnApplyClick(Sender: TObject);
    procedure btnCanClick(Sender: TObject);
    procedure btnReadClick(Sender: TObject);
  private
    FStartupLogged: Boolean; { 自检日志只打一次。 }
    FPort: TComPort;
    FParser: THlParser;      { HL1 逐字节收帧。 }
    FTimer: TTimer;          { 50 ms 收串口，约 1 s 发一次探活或读状态。 }
    FSeq: Cardinal;          { 主机序号，从 1 开始，不用 0。 }
    FSeen: Boolean;          { 已经收到过合法应答。 }
    FMiss: Integer;          { 连续未应答的秒数计数。 }
    FTicks: Integer;         { 50 ms 拍子，满 20 拍约 1 秒。 }
    FLinkText: string;       { 状态栏中间一格。 }
    FLastTick: Cardinal;     { 上次写进日志的状态，用来去重。 }
    FLastAdc0: Cardinal;
    FLastAdc1: Cardinal;
    FLastInputs: Cardinal;
    FLastOutputs: Cardinal;
    FLastDac: Cardinal;
    FHaveSample: Boolean;
    procedure ApplyDpiLayout;
    procedure RefreshDpiStatus;
    procedure LogLine(const AText: string);
    procedure ReloadPorts;
    procedure DisconnectPort;
    procedure PollTimer(Sender: TObject);
    procedure SendEnv(const Msg: THlEnvelope);
    procedure SendSimple(Command: Cardinal);
    function WantedBoard: Byte;
    function RequireLink: Boolean;
    procedure HandleFrame;
  public
    { DPI 变化时重算状态栏和分割条的最小宽度。 }
    procedure AutoAdjustLayout(AMode: TLayoutAdjustmentPolicy; const AFromPPI,
      AToPPI, AOldFormWidth, ANewFormWidth: Integer); override;
  end;

var
  MainForm: TMainForm;

implementation

uses
  uDpi;

{$R *.lfm}

const
  StatusPanelWidth: array[0..2] of Integer = (300, 220, 440); { 按 96 PPI 计的三格宽度。 }
  SideMinWidth = 280;                                          { 左侧面板最小宽度，同样按 96 PPI。 }

{ 十进制、0x 或 $ 前缀都接受。超出 MaxValue 返回 False。 }
function ParseUint(const Text: string; MaxValue: Cardinal; out Value: Cardinal): Boolean;
var
  S: string;
  N: Int64;
begin
  S := Trim(Text);
  Result := False;
  if S = '' then
    Exit;
  if (Length(S) > 2) and (S[1] = '0') and ((S[2] = 'x') or (S[2] = 'X')) then
  begin
    if not TryStrToInt64('$' + Copy(S, 3, 16), N) then { 0x 改成 FPC 认识的 $。 }
      Exit;
  end
  else if S[1] = '$' then
  begin
    if not TryStrToInt64(S, N) then
      Exit;
  end
  else if not TryStrToInt64(S, N) then
    Exit;
  if (N < 0) or (N > MaxValue) then
    Exit;
  Value := Cardinal(N);
  Result := True;
end;

{ 偶数位十六进制，空格可忽略。超过 8 字节失败。 }
function ParseHex(const Text: string; out Data: TBytes): Boolean;
var
  S: string;
  I, Nibble, Value: Integer;
  Hi: Boolean;
begin
  S := '';
  for I := 1 to Length(Text) do
    if not (Text[I] in [' ', #9]) then
      S := S + Text[I];                 { 去掉空格和制表符。 }
  Result := False;
  if (S = '') or ((Length(S) mod 2) <> 0) or (Length(S) > HL_CAN_MAX * 2) then
    Exit;                               { 空、奇数位、或超过 8 字节都不合法。 }
  SetLength(Data, Length(S) div 2);
  Hi := True;                           { 先读高半字节。 }
  Value := 0;
  Nibble := 0;
  for I := 1 to Length(S) do
  begin
    case S[I] of
      '0'..'9': Nibble := Ord(S[I]) - Ord('0');
      'a'..'f': Nibble := Ord(S[I]) - Ord('a') + 10;
      'A'..'F': Nibble := Ord(S[I]) - Ord('A') + 10;
    else
      Exit;                             { 非十六进制字符。 }
    end;
    if Hi then
      Value := Nibble shl 4
    else
    begin
      Data[(I div 2) - 1] := Byte(Value or Nibble); { 高低半字节合成一个字节。 }
    end;
    Hi := not Hi;
  end;
  Result := True;
end;

{ 把若干字节打成大写十六进制，字节之间空一格。 }
function HexOf(const Data: array of Byte; Count: Integer): string;
const
  Digits = '0123456789ABCDEF';
var
  I: Integer;
begin
  Result := '';
  for I := 0 to Count - 1 do
  begin
    if Result <> '' then
      Result := Result + ' ';
    Result := Result + Digits[(Data[I] shr 4) + 1] + Digits[(Data[I] and $0F) + 1];
  end;
end;

procedure TMainForm.ApplyDpiLayout;
var
  I: Integer;
begin
  for I := 0 to StatusBar1.Panels.Count - 1 do
    StatusBar1.Panels[I].Width := ScaleDesign(Self, StatusPanelWidth[I]); { 运行期覆盖 lfm 里的宽度。 }
  splMain.MinSize := ScaleDesign(Self, SideMinWidth);
end;

procedure TMainForm.RefreshDpiStatus;
var
  Mon: TMonitor;
  MonText: string;
begin
  Mon := Monitor;
  if Mon = nil then
    MonText := '无显示器'
  else
    MonText := Format('%d × %d px，%d PPI', [Mon.Width, Mon.Height, Mon.PixelsPerInch]);
  StatusBar1.Panels[0].Text := Format('窗体 %d PPI，缩放 %d%%',
    [PixelsPerInch, CurrentScalePercent(Self)]);
  StatusBar1.Panels[1].Text := FLinkText; { 连接状态。 }
  StatusBar1.Panels[2].Text := MonText;
end;

procedure TMainForm.LogLine(const AText: string);
begin
  memLog.Lines.Add(FormatDateTime('hh:nn:ss', Now) + '  ' + AText);
  if memLog.Lines.Count > 400 then
    memLog.Lines.Delete(0);            { 只留最近 400 行。 }
  memLog.SelStart := Length(memLog.Text); { 滚到末尾。 }
end;

procedure TMainForm.ReloadPorts;
var
  Current: string;
  Index: Integer;
begin
  Current := cmbPort.Text;             { 刷新后尽量留在原来的口。 }
  ListComPorts(cmbPort.Items);
  if cmbPort.Items.Count = 0 then
  begin
    cmbPort.Items.Add('（无可用串口）');
    cmbPort.ItemIndex := 0;
    Exit;
  end;
  Index := cmbPort.Items.IndexOf(Current);
  if Index < 0 then
    Index := 0;
  cmbPort.ItemIndex := Index;
end;

procedure TMainForm.DisconnectPort;
begin
  if FTimer <> nil then
    FTimer.Enabled := False;           { 先停轮询，再关串口。 }
  if FPort <> nil then
    FPort.Close;
  FSeen := False;
  FLinkText := '未连接';
  btnConnect.Caption := '连接';
  RefreshDpiStatus;
end;

function TMainForm.WantedBoard: Byte;
begin
  case cmbBoard.ItemIndex of
    1: Result := HL_BOARD_RX71M;       { 只收 RX71M 的帧。 }
    2: Result := HL_BOARD_RA8P;
  else
    Result := HL_BOARD_ANY;            { 0：两块板都收。 }
  end;
end;

function TMainForm.RequireLink: Boolean;
begin
  Result := (FPort <> nil) and FPort.IsOpen;
  if not Result then
    LogLine('还没有连接串口。');
end;

procedure TMainForm.SendEnv(const Msg: THlEnvelope);
var
  Body: THlEnvelope;
  Payload, Frame: TBytes;
begin
  if not RequireLink then
    Exit;
  Body := Msg;
  Body.Sequence := FSeq;               { 序号放进 Protobuf，帧头只用低 8 位。 }
  Payload := HlEncodeEnvelope(Body);
  Frame := HlEncodeFrame(WantedBoard, 0, Byte(FSeq and $FF), Payload); { 主机发出的帧标志为 0。 }
  Inc(FSeq);
  if FSeq = 0 then
    FSeq := 1;                         { 绕回时跳过 0，0 留给“未填序号”。 }
  if not FPort.WriteBytes(Frame) then
  begin
    LogLine('发送失败：' + FPort.LastError);
    DisconnectPort;
  end;
end;

procedure TMainForm.SendSimple(Command: Cardinal);
var
  Msg: THlEnvelope;
begin
  FillChar(Msg, SizeOf(Msg), 0);       { 局部记录的字符串仍是空的，可以整块清零。 }
  Msg.Command := Command;              { PING 或 SNAPSHOT，没有子消息。 }
  SendEnv(Msg);
end;

procedure TMainForm.HandleFrame;
var
  Payload: TBytes;
  Msg: THlEnvelope;
  Fresh: Boolean;
begin
  if (WantedBoard <> HL_BOARD_ANY) and (FParser.Board <> HL_BOARD_ANY) and
     (FParser.Board <> WantedBoard) then
  begin
    LogLine('收到 ' + HlBoardName(FParser.Board) + '，与所选目标不一致，这帧已忽略。');
    Exit;
  end;
  SetLength(Payload, FParser.Len);
  if FParser.Len > 0 then
    Move(FParser.Payload[0], Payload[0], FParser.Len);
  if not HlDecodeEnvelope(Payload, Msg) then
  begin
    LogLine('CRC 通过，但 Protobuf 载荷无法解析。');
    Exit;
  end;
  FSeen := True;                       { 链路已经打通，后面改发 SNAPSHOT。 }
  if Msg.HasSnapshot then
  begin
    FLinkText := HlBoardName(Msg.Board) + ' 已连接';
    if Msg.CanFd then
      FLinkText := FLinkText + '，CAN FD'; { RA8P 的状态里这一位为真。 }
    Fresh := (not FHaveSample) or (Msg.Tick <> FLastTick) or (Msg.Adc0 <> FLastAdc0) or
      (Msg.Adc1 <> FLastAdc1) or (Msg.Inputs <> FLastInputs) or
      (Msg.Outputs <> FLastOutputs) or (Msg.Dac <> FLastDac);
    FLastTick := Msg.Tick;
    FLastAdc0 := Msg.Adc0;
    FLastAdc1 := Msg.Adc1;
    FLastInputs := Msg.Inputs;
    FLastOutputs := Msg.Outputs;
    FLastDac := Msg.Dac;
    FHaveSample := True;
    if Fresh then                      { 数值没变就不刷屏。 }
      LogLine(Format('%s  tick=%u  ADC=%u/%u  IN=%s  OUT=%s  DAC=%u',
        [HlBoardName(Msg.Board), Msg.Tick, Msg.Adc0, Msg.Adc1,
         HexOf([Byte(Msg.Inputs)], 1), HexOf([Byte(Msg.Outputs)], 1), Msg.Dac]));
  end;
  if Msg.HasAck then
  begin
    case Msg.AckStatus of
      HL_ACK_OK: LogLine('应答：成功');
      HL_ACK_UNSUPPORTED:
        if Msg.AckDetail = 'can fd unsupported' then
          LogLine('应答：这块板是经典 CAN，不能发 CAN FD。')
        else
          LogLine('应答：板卡不支持。' + Msg.AckDetail);
      HL_ACK_BAD_ARG: LogLine('应答：参数无效。' + Msg.AckDetail);
    else
      LogLine('应答：状态 ' + IntToStr(Msg.AckStatus));
    end;
  end;
  if Msg.HasCanLog then
    LogLine(Format('CAN%s  ch%u  ID=$%x  %s  %s',
      [BoolToStr(Msg.LogFd, ' FD', ''), Msg.LogChannel, Msg.LogId,
       HexOf(Msg.LogData, Msg.LogDlc), BoolToStr(Msg.LogTx, '发送', '接收')]));
  RefreshDpiStatus;
end;

procedure TMainForm.PollTimer(Sender: TObject);
var
  Buf: array[0..255] of Byte;
  N, I: Integer;
begin
  if (FPort = nil) or not FPort.IsOpen then
    Exit;
  N := FPort.ReadBytes(Buf);           { 这一拍有多少就读多少。 }
  for I := 0 to N - 1 do
    if FParser.Push(Buf[I]) then       { CRC 正确才处理。 }
      HandleFrame;
  Inc(FTicks);
  if FTicks < 20 then
    Exit;                              { 未满约 1 秒，只收不发。 }
  FTicks := 0;
  if not FSeen then
  begin
    Inc(FMiss);
    if FMiss = 2 then                  { 大约两秒仍无应答。 }
      LogLine('两秒内没有应答。请接业务串口，115200 8N1，并确认板卡程序已运行。');
    SendSimple(HL_CMD_PING);
  end
  else
    SendSimple(HL_CMD_SNAPSHOT);       { 已连上后每秒要一次状态。 }
end;

procedure TMainForm.AutoAdjustLayout(AMode: TLayoutAdjustmentPolicy;
  const AFromPPI, AToPPI, AOldFormWidth, ANewFormWidth: Integer);
begin
  inherited AutoAdjustLayout(AMode, AFromPPI, AToPPI, AOldFormWidth, ANewFormWidth);
  if AMode = lapAutoAdjustForDPI then  { 只有 DPI 变化才重算，普通拉窗口不进来。 }
  begin
    ApplyDpiLayout;
    if HandleAllocated then
      RefreshDpiStatus;
  end;
end;

procedure TMainForm.FormShow(Sender: TObject);
begin
  if FLinkText = '' then
    FLinkText := '未连接';
  if cmbBoard.ItemIndex < 0 then
    cmbBoard.ItemIndex := 0;           { 默认“自动”。 }
  if cmbPwm.ItemIndex < 0 then
    cmbPwm.ItemIndex := 0;
  if cmbCan.ItemIndex < 0 then
    cmbCan.ItemIndex := 0;
  ApplyDpiLayout;
  RefreshDpiStatus;
  if FStartupLogged then
    Exit;                              { 窗体再次显示时不重复建对象。 }
  FStartupLogged := True;
  FPort := TComPort.Create;
  FParser := THlParser.Create;
  FTimer := TTimer.Create(Self);       { 挂在窗体上，窗体释放时一起释放。 }
  FTimer.Interval := 50;
  FTimer.Enabled := False;             { 连接成功后才开始转。 }
  FTimer.OnTimer := @PollTimer;
  FSeq := 1;
  ReloadPorts;
  LogLine(Format('界面 %d PPI（%d%%）。', [PixelsPerInch, CurrentScalePercent(Self)]));
  if HostLinkMatchesProtoc then
    LogLine('Protobuf 36.2 线格式自检通过。')
  else
    LogLine('Protobuf 线格式自检失败。');
  LogLine('业务串口 115200 8N1。同一套 HL1 帧连接 RX71M 和 RA8P，CAN FD 只发给 RA8P。');
end;

procedure TMainForm.FormDestroy(Sender: TObject);
begin
  DisconnectPort;
  FParser.Free;
  FPort.Free;                          { 定时器属于窗体，这里不单独释放。 }
end;

procedure TMainForm.btnRefreshClick(Sender: TObject);
begin
  ReloadPorts;
  LogLine('串口列表已刷新。');
end;

procedure TMainForm.btnConnectClick(Sender: TObject);
begin
  if (FPort <> nil) and FPort.IsOpen then
  begin
    DisconnectPort;                    { 再按一次就是断开。 }
    LogLine('已断开。');
    Exit;
  end;
  if (cmbPort.Text = '') or (Pos('COM', cmbPort.Text) <> 1) then
  begin
    LogLine('请先刷新并选择一个 COM 口。');
    Exit;
  end;
  if not FPort.Open(cmbPort.Text) then
  begin
    LogLine('打开 ' + cmbPort.Text + ' 失败：' + FPort.LastError);
    Exit;
  end;
  FParser.Reset;                       { 丢掉上次没收完的半帧。 }
  FSeen := False;
  FMiss := 0;
  FTicks := 0;
  FHaveSample := False;
  FSeq := 1;
  FLinkText := cmbPort.Text + ' 等待应答';
  btnConnect.Caption := '断开';
  RefreshDpiStatus;
  FTimer.Enabled := True;
  LogLine('已打开 ' + cmbPort.Text + '，正在问候板卡。');
  SendSimple(HL_CMD_PING);             { 马上发一次，不等满 1 秒。 }
end;

procedure TMainForm.btnApplyClick(Sender: TObject);
var
  Mask, Code, Duty, Channel: Cardinal;
  Msg: THlEnvelope;
begin
  if not RequireLink then
    Exit;
  if not ParseUint(edtGpio.Text, 15, Mask) then
  begin
    LogLine('GPIO 掩码要填 0 到 15。');
    Exit;
  end;
  if not ParseUint(edtDac.Text, 4095, Code) then
  begin
    LogLine('DAC 要填 0 到 4095。');
    Exit;
  end;
  if not ParseUint(edtDuty.Text, 1000, Duty) then
  begin
    LogLine('占空比要填 0 到 1000。');
    Exit;
  end;
  Channel := Cardinal(cmbPwm.ItemIndex);
  if cmbPwm.ItemIndex < 0 then
    Channel := 0;
  FillChar(Msg, SizeOf(Msg), 0);
  Msg.Command := HL_CMD_SET_OUTPUTS;
  Msg.HasSetOutputs := True;
  Msg.OutputsMask := Mask;             { 0 也要发出去，用来清掉全部输出。 }
  SendEnv(Msg);
  FillChar(Msg, SizeOf(Msg), 0);       { 每条命令单独一帧。 }
  Msg.Command := HL_CMD_SET_DAC;
  Msg.HasSetDac := True;
  Msg.DacCode := Code;
  SendEnv(Msg);
  FillChar(Msg, SizeOf(Msg), 0);
  Msg.Command := HL_CMD_SET_PWM;
  Msg.HasSetPwm := True;
  Msg.PwmChannel := Channel;
  Msg.PwmDuty := Duty;
  SendEnv(Msg);
  LogLine(Format('已下发 GPIO=%u DAC=%u PWM%u=%u‰。', [Mask, Code, Channel, Duty]));
end;

procedure TMainForm.btnCanClick(Sender: TObject);
var
  Id, Channel: Cardinal;
  Data: TBytes;
  Msg: THlEnvelope;
  I: Integer;
begin
  if not RequireLink then
    Exit;
  if not ParseUint(edtCanId.Text, $7FF, Id) then
  begin
    LogLine('CAN ID 使用 11 位标准帧，0 到 $7FF。');
    Exit;
  end;
  if not ParseHex(edtCanData.Text, Data) then
  begin
    LogLine('CAN 数据要写成偶数位十六进制，最多 8 字节。');
    Exit;
  end;
  Channel := 0;
  if cmbCan.ItemIndex > 0 then
    Channel := Cardinal(cmbCan.ItemIndex);
  FillChar(Msg, SizeOf(Msg), 0);
  Msg.Command := HL_CMD_SEND_CAN;
  Msg.HasSendCan := True;
  Msg.CanChannel := Channel;
  Msg.CanId := Id;
  Msg.CanDlc := Byte(Length(Data));
  for I := 0 to Length(Data) - 1 do
    Msg.CanData[I] := Data[I];
  Msg.CanReqFd := chkCanFd.Checked;    { RX71M 收到后会回“不支持”。 }
  SendEnv(Msg);
  if chkCanFd.Checked then
    LogLine('已请求 CAN FD。RX71M 会拒绝，RA8P 会发送。')
  else
    LogLine('已请求经典 CAN。');
end;

procedure TMainForm.btnReadClick(Sender: TObject);
begin
  if not RequireLink then
    Exit;
  SendSimple(HL_CMD_SNAPSHOT);         { 不等 1 秒定时器，立刻要一次状态。 }
end;

end.

unit uComPort;

{
  Windows 串口。上位机走板卡业务 UART：115200 8N1，无校验，无流控。
  日志口不在这里打开。
}

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils;

type
  TComPort = class
  public
    constructor Create;
    destructor Destroy; override;
    function Open(const PortName: string): Boolean;       { 打开 COMx。 }
    procedure Close;
    function IsOpen: Boolean;
    function WriteBytes(const Data: TBytes): Boolean;     { 整帧一次写完。 }
    function ReadBytes(var Buf: array of Byte): Integer;  { 读到多少返回多少，没有数据返回 0。 }
    function LastError: string;
  private
    FHandle: THandle; { INVALID_HANDLE_VALUE 表示没打开。 }
    FError: string;   { 最近一次 Win32 错误的文字。 }
  end;

{ 枚举当前存在的 COM 口，按编号排序后放进 Dest。 }
procedure ListComPorts(Dest: TStrings);

implementation

uses
  Windows;

constructor TComPort.Create;
begin
  inherited Create;
  FHandle := INVALID_HANDLE_VALUE; { 还没有打开任何口。 }
end;

destructor TComPort.Destroy;
begin
  Close;             { 窗体销毁时把句柄还回去。 }
  inherited Destroy;
end;

function TComPort.IsOpen: Boolean;
begin
  Result := FHandle <> INVALID_HANDLE_VALUE;
end;

function TComPort.LastError: string;
begin
  Result := FError;
end;

function TComPort.Open(const PortName: string): Boolean;
var
  Dcb: TDCB;
  Timeouts: TCOMMTIMEOUTS;
  Name: string;
begin
  Result := False;
  Close;                              { 已打开则先关掉，避免句柄泄漏。 }
  FError := '';
  Name := '\\.\' + PortName;          { COM10 及以上必须带这个前缀。 }
  FHandle := CreateFile(PChar(Name), GENERIC_READ or GENERIC_WRITE, 0, nil,
    OPEN_EXISTING, 0, 0);             { 独占打开已有设备，不用重叠 I/O。 }
  if FHandle = INVALID_HANDLE_VALUE then
  begin
    FError := SysErrorMessage(GetLastError);
    Exit;
  end;
  FillChar(Dcb, SizeOf(Dcb), 0);
  Dcb.DCBlength := SizeOf(Dcb);
  if not BuildCommDCB(PChar('baud=115200 parity=N data=8 stop=1'), Dcb) then
  begin
    FError := SysErrorMessage(GetLastError); { 波特率字符串解析失败。 }
    Close;
    Exit;
  end;
  Dcb.Flags := 1;                     { 只留 fBinary，关掉流控和奇偶。 }
  if not SetCommState(FHandle, Dcb) then
  begin
    FError := SysErrorMessage(GetLastError);
    Close;
    Exit;
  end;
  FillChar(Timeouts, SizeOf(Timeouts), 0);
  Timeouts.ReadIntervalTimeout := MAXDWORD; { 读操作立即返回，没有字节时 Got=0。 }
  Timeouts.WriteTotalTimeoutConstant := 200; { 写整帧最多等 200 ms。 }
  if not SetCommTimeouts(FHandle, Timeouts) then
  begin
    FError := SysErrorMessage(GetLastError);
    Close;
    Exit;
  end;
  SetupComm(FHandle, 4096, 4096);     { 收发缓冲各 4 KB。 }
  PurgeComm(FHandle, PURGE_RXCLEAR or PURGE_TXCLEAR); { 丢掉打开前残留的字节。 }
  Result := True;
end;

procedure TComPort.Close;
begin
  if FHandle <> INVALID_HANDLE_VALUE then
  begin
    CloseHandle(FHandle);
    FHandle := INVALID_HANDLE_VALUE;
  end;
end;

function TComPort.WriteBytes(const Data: TBytes): Boolean;
var
  Written: DWORD;
begin
  Result := False;
  if (not IsOpen) or (Length(Data) = 0) then
    Exit;                             { 空帧不写。 }
  if not WriteFile(FHandle, Data[0], DWORD(Length(Data)), Written, nil) or
     (Written <> DWORD(Length(Data))) then
  begin
    FError := SysErrorMessage(GetLastError); { 没写全也当失败。 }
    Exit;
  end;
  Result := True;
end;

function TComPort.ReadBytes(var Buf: array of Byte): Integer;
var
  Got: DWORD;
begin
  Result := 0;
  if not IsOpen then
    Exit;
  if not ReadFile(FHandle, Buf[0], DWORD(Length(Buf)), Got, nil) then
  begin
    FError := SysErrorMessage(GetLastError);
    Exit;
  end;
  Result := Integer(Got);             { 0 表示这一拍没有新字节。 }
end;

{ 从 "COM12" 里取出 12，用来排序。没有数字时排到最后。 }
function ComIndex(const Name: string): Integer;
var
  I: Integer;
  Digits: string;
begin
  Digits := '';
  for I := 1 to Length(Name) do
    if Name[I] in ['0'..'9'] then
      Digits := Digits + Name[I];
  Result := StrToIntDef(Digits, 9999);
end;

procedure ListComPorts(Dest: TStrings);
var
  Buf: array[0..32767] of Char;
  P: PChar;
  Names: TStringList;
  I, J: Integer;
begin
  Dest.Clear;
  if QueryDosDevice(nil, Buf, Length(Buf)) = 0 then
    Exit;                             { 枚举失败就留空列表。 }
  Names := TStringList.Create;
  try
    P := Buf;                         { 结果是以双 0 结尾的设备名列表。 }
    while P^ <> #0 do
    begin
      if (StrLen(P) >= 4) and (Copy(string(P), 1, 3) = 'COM') then
        Names.Add(string(P));         { 只要 COM 开头的名字。 }
      Inc(P, StrLen(P) + 1);          { 跳到下一个以 0 分隔的名字。 }
    end;
    for I := 0 to Names.Count - 2 do  { 按 COM 编号从小到大。 }
      for J := I + 1 to Names.Count - 1 do
        if ComIndex(Names[J]) < ComIndex(Names[I]) then
          Names.Exchange(I, J);
    Dest.Assign(Names);
  finally
    Names.Free;
  end;
end;

end.

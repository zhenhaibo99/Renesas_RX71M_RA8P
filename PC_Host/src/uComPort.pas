unit uComPort;

{
  Windows 串口。上位机走板卡业务 UART：115200 8N1，无校验，无流控。
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
    function Open(const PortName: string): Boolean;
    procedure Close;
    function IsOpen: Boolean;
    function WriteBytes(const Data: TBytes): Boolean;
    function ReadBytes(var Buf: array of Byte): Integer;
    function LastError: string;
  private
    FHandle: THandle;
    FError: string;
  end;

procedure ListComPorts(Dest: TStrings);

implementation

uses
  Windows;

constructor TComPort.Create;
begin
  inherited Create;
  FHandle := INVALID_HANDLE_VALUE;
end;

destructor TComPort.Destroy;
begin
  Close;
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
  Close;
  FError := '';
  Name := '\\.\' + PortName;
  FHandle := CreateFile(PChar(Name), GENERIC_READ or GENERIC_WRITE, 0, nil,
    OPEN_EXISTING, 0, 0);
  if FHandle = INVALID_HANDLE_VALUE then
  begin
    FError := SysErrorMessage(GetLastError);
    Exit;
  end;
  FillChar(Dcb, SizeOf(Dcb), 0);
  Dcb.DCBlength := SizeOf(Dcb);
  if not BuildCommDCB(PChar('baud=115200 parity=N data=8 stop=1'), Dcb) then
  begin
    FError := SysErrorMessage(GetLastError);
    Close;
    Exit;
  end;
  Dcb.Flags := 1;
  if not SetCommState(FHandle, Dcb) then
  begin
    FError := SysErrorMessage(GetLastError);
    Close;
    Exit;
  end;
  FillChar(Timeouts, SizeOf(Timeouts), 0);
  Timeouts.ReadIntervalTimeout := MAXDWORD;
  Timeouts.WriteTotalTimeoutConstant := 200;
  if not SetCommTimeouts(FHandle, Timeouts) then
  begin
    FError := SysErrorMessage(GetLastError);
    Close;
    Exit;
  end;
  SetupComm(FHandle, 4096, 4096);
  PurgeComm(FHandle, PURGE_RXCLEAR or PURGE_TXCLEAR);
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
    Exit;
  if not WriteFile(FHandle, Data[0], DWORD(Length(Data)), Written, nil) or
     (Written <> DWORD(Length(Data))) then
  begin
    FError := SysErrorMessage(GetLastError);
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
  Result := Integer(Got);
end;

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
    Exit;
  Names := TStringList.Create;
  try
    P := Buf;
    while P^ <> #0 do
    begin
      if (StrLen(P) >= 4) and (Copy(string(P), 1, 3) = 'COM') then
        Names.Add(string(P));
      Inc(P, StrLen(P) + 1);
    end;
    for I := 0 to Names.Count - 2 do
      for J := I + 1 to Names.Count - 1 do
        if ComIndex(Names[J]) < ComIndex(Names[I]) then
          Names.Exchange(I, J);
    Dest.Assign(Names);
  finally
    Names.Free;
  end;
end;

end.

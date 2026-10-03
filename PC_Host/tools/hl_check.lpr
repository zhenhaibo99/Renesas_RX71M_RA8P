program hl_check;

{
  命令行自检。编码结果必须和 protoc 36.2 的黄金字节一致。
  由 Lazarus 工程外单独编译，不进主程序窗体。
}

uses
  SysUtils,   { Halt、WriteLn。 }
  uHostLink;  { HostLinkMatchesProtoc。 }

begin
  if not HostLinkMatchesProtoc then
  begin
    WriteLn('FAIL'); { 任一黄金向量或回环失败。 }
    Halt(1);         { 非 0 退出码，方便脚本判断。 }
  end;
  WriteLn('ok');     { 全部通过。 }
end.

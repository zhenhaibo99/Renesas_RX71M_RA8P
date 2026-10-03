program hl_check;

uses
  SysUtils, uHostLink;

begin
  if not HostLinkMatchesProtoc then
  begin
    WriteLn('FAIL');
    Halt(1);
  end;
  WriteLn('ok');
end.

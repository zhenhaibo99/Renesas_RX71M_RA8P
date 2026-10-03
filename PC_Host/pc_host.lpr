program pc_host;

{
  Lazarus 4.8（LCL 4.8.0.0，FPC 3.2.2）上位机模板。
  工程选项已打开 LCL 缩放，Windows 清单为 Per-Monitor V2。
  窗体按 96 PPI 设计；系统 150% 时运行期为 144 PPI。
}

{$mode objfpc}{$H+}

uses
  {$IFDEF UNIX}
  cthreads,
  {$ENDIF}
  {$IFDEF HASAMIGA}
  athreads,
  {$ENDIF}
  Interfaces, // LCL widgetset
  Forms,
  uMain;

{$R *.res}

begin
  RequireDerivedFormResource := True;
  Application.Scaled := True;
  Application.Title := 'PC 上位机';
  Application.Initialize;
  Application.CreateForm(TMainForm, MainForm);
  Application.Run;
end.

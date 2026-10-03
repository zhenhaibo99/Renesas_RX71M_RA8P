program pc_host;

{
  Lazarus 4.8（LCL 4.8.0.0，FPC 3.2.2）上位机。
  工程选项已打开 LCL 缩放，Windows 清单为 Per-Monitor V2。
  窗体按设计期 PPI 摆放，运行期随系统缩放。
}

{$mode objfpc}{$H+}

uses
  {$IFDEF UNIX}
  cthreads,   { Unix 下 LCL 需要这个线程单元。 }
  {$ENDIF}
  {$IFDEF HASAMIGA}
  athreads,   { Amiga 下的对应单元，本工程在 Windows 上不会编译进来。 }
  {$ENDIF}
  Interfaces, { LCL 控件接口。 }
  Forms,      { Application、TForm。 }
  uMain;      { 主窗体。 }

{$R *.res}    { 嵌入 Windows 清单等资源。 }

begin
  RequireDerivedFormResource := True;          { 必须从 lfm 加载窗体，缺资源就报错。 }
  Application.Scaled := True;                  { 按当前显示器 PPI 缩放窗体。 }
  Application.Title := 'PC 上位机';            { 任务栏标题。 }
  Application.Initialize;                      { 初始化 LCL。 }
  Application.CreateForm(TMainForm, MainForm); { 创建主窗体。 }
  Application.Run;                             { 进入消息循环，直到窗体关闭。 }
end.

unit uMain;

{
  主窗体在 96 PPI 下摆放，Scaled = True。
  打开 LCL 缩放且清单为 Per-Monitor V2 后，LCL 会按当前显示器 PPI 缩放
  控件、字体和 Constraints。4K 上常用 150%（144 PPI）或 200%（192 PPI），
  把窗口拖到另一块缩放不同的屏幕时也会重排，不走 Windows 位图拉伸。
}

{$mode objfpc}{$H+}

interface

uses
  Classes, SysUtils, Forms, Controls, Graphics, Dialogs, StdCtrls, ExtCtrls,
  ComCtrls;

type

  { TMainForm }

  TMainForm = class(TForm)
    pnlTop: TPanel;
    lblTitle: TLabel;
    lblSub: TLabel;
    StatusBar1: TStatusBar;
    pnlSide: TPanel;
    splMain: TSplitter;
    grpLink: TGroupBox;
    lblChannel: TLabel;
    cmbPort: TComboBox;
    lblNote: TLabel;
    btnRefresh: TButton;
    btnConnect: TButton;
    memLog: TMemo;
    procedure FormShow(Sender: TObject);
    procedure btnRefreshClick(Sender: TObject);
    procedure btnConnectClick(Sender: TObject);
  private
    FStartupLogged: Boolean;
    procedure ApplyDpiLayout;
    procedure RefreshDpiStatus;
    procedure LogLine(const AText: string);
  public
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
  StatusPanelWidth: array[0..2] of Integer = (300, 180, 480);
  SideMinWidth = 220;

procedure TMainForm.ApplyDpiLayout;
var
  I: Integer;
begin
  for I := 0 to StatusBar1.Panels.Count - 1 do
    StatusBar1.Panels[I].Width := ScaleDesign(Self, StatusPanelWidth[I]);
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
  StatusBar1.Panels[1].Text := Format('设计基准 %d PPI', [DesignPPI]);
  StatusBar1.Panels[2].Text := MonText;
end;

procedure TMainForm.LogLine(const AText: string);
begin
  memLog.Lines.Add(FormatDateTime('hh:nn:ss', Now) + '  ' + AText);
end;

procedure TMainForm.AutoAdjustLayout(AMode: TLayoutAdjustmentPolicy;
  const AFromPPI, AToPPI, AOldFormWidth, ANewFormWidth: Integer);
begin
  inherited AutoAdjustLayout(AMode, AFromPPI, AToPPI, AOldFormWidth, ANewFormWidth);
  if AMode = lapAutoAdjustForDPI then
  begin
    ApplyDpiLayout;
    if HandleAllocated then
      RefreshDpiStatus;
  end;
end;

procedure TMainForm.FormShow(Sender: TObject);
begin
  if cmbPort.Items.Count > 0 then
    cmbPort.ItemIndex := 0;
  ApplyDpiLayout;
  RefreshDpiStatus;
  if not FStartupLogged then
  begin
    FStartupLogged := True;
    LogLine(Format('模板已启动。设计 %d PPI，当前窗体 %d PPI（%d%%）。',
      [DesignPPI, PixelsPerInch, CurrentScalePercent(Self)]));
    LogLine('清单为 Per-Monitor V2。150% 缩放时应看到 144 PPI；4K 全屏分辨率仍由显示器自己的 PPI 决定。');
  end;
end;

procedure TMainForm.btnRefreshClick(Sender: TObject);
begin
  LogLine('刷新通道：模板未绑定 USB / 串口。');
  RefreshDpiStatus;
end;

procedure TMainForm.btnConnectClick(Sender: TObject);
begin
  LogLine('连接预留。当前选择：' + cmbPort.Text);
end;

end.

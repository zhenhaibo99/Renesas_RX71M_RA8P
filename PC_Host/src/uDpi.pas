unit uDpi;

{
  状态栏宽度等运行期尺寸按 96 PPI 书写。
  Windows 缩放 100% = 96 PPI，150% = 144 PPI，200% = 192 PPI。
  Scale96ToForm 的基准始终是 96，与 lfm 里的 DesignTimePPI 无关。
}

{$mode objfpc}{$H+}

interface

uses
  Forms;

const
  DesignPPI = 96;    { 设计基准，用来把 PPI 换算成百分比。 }
  Scale150PPI = 144; { 150% 对应的 PPI，留给对照，运行期不直接使用。 }

{ 把 96 PPI 下的像素换成当前窗体上的像素。 }
function ScaleDesign(AForm: TCustomForm; ADesignPx: Integer): Integer;
{ 当前缩放百分比。100 表示不缩放。 }
function CurrentScalePercent(AForm: TCustomForm): Integer;

implementation

function ScaleDesign(AForm: TCustomForm; ADesignPx: Integer): Integer;
begin
  if AForm = nil then
    Exit(ADesignPx);                         { 没有窗体时原样返回。 }
  Result := AForm.Scale96ToForm(ADesignPx); { LCL 按 96 到当前 PPI 换算。 }
end;

function CurrentScalePercent(AForm: TCustomForm): Integer;
var
  PPI: Integer;
begin
  PPI := DesignPPI;                                              { 先假定 100%。 }
  if (AForm <> nil) and (AForm.PixelsPerInch > 0) then
    PPI := AForm.PixelsPerInch                                    { 优先用窗体自己的 PPI。 }
  else if Screen.PixelsPerInch > 0 then
    PPI := Screen.PixelsPerInch;                                  { 窗体还没有 PPI 时用屏幕。 }
  Result := (PPI * 100) div DesignPPI;                           { 144 PPI 得到 150。 }
end;

end.

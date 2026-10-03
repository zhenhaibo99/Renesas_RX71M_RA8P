unit uDpi;

{
  设计期坐标一律按 96 PPI 书写。
  Windows 缩放 100% = 96 PPI，150% = 144 PPI，200% = 192 PPI。
  运行期创建控件、计算宽度时调用 ScaleDesign，不要把 150% 的像素写进源码。
}

{$mode objfpc}{$H+}

interface

uses
  Forms;

const
  DesignPPI = 96;
  Scale150PPI = 144;

function ScaleDesign(AForm: TCustomForm; ADesignPx: Integer): Integer;
function CurrentScalePercent(AForm: TCustomForm): Integer;

implementation

function ScaleDesign(AForm: TCustomForm; ADesignPx: Integer): Integer;
begin
  if AForm = nil then
    Exit(ADesignPx);
  Result := AForm.Scale96ToForm(ADesignPx);
end;

function CurrentScalePercent(AForm: TCustomForm): Integer;
var
  PPI: Integer;
begin
  PPI := DesignPPI;
  if (AForm <> nil) and (AForm.PixelsPerInch > 0) then
    PPI := AForm.PixelsPerInch
  else if Screen.PixelsPerInch > 0 then
    PPI := Screen.PixelsPerInch;
  Result := (PPI * 100) div DesignPPI;
end;

end.

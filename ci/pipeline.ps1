# Local and CI entry for this repository.
# ASCII only so Windows PowerShell 5.1 can parse it without a BOM.
[CmdletBinding()]
param(
    # Comma-separated: all, protocol, host, firmware. -File does not split arrays.
    [string]$Stage = 'all',
    [ValidateSet('Debug', 'Release', 'both')]
    [string]$Configuration = 'both'
)

$ErrorActionPreference = 'Stop'
$Root = (Resolve-Path (Join-Path $PSScriptRoot '..')).Path
$OutDir = Join-Path $PSScriptRoot 'out'
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null
$Summary = Join-Path $OutDir 'summary.txt'
$script:Failed = $false

function Write-StageResult {
    param([string]$Name, [string]$Status, [string]$Detail)
    $line = '{0} {1} {2}' -f $Status, $Name, $Detail
    Add-Content -Path $Summary -Encoding utf8 -Value $line
    Write-Host $line
}

$StageNames = @($Stage.Split(',') | ForEach-Object { $_.Trim() } | Where-Object { $_ -ne '' })
foreach ($item in $StageNames) {
    if (@('all', 'protocol', 'host', 'firmware') -notcontains $item) {
        throw "Unknown stage '$item'. Use all, protocol, host, or firmware."
    }
}

function Test-Selected {
    param([string]$Name)
    return ($StageNames -contains 'all') -or ($StageNames -contains $Name)
}

function Invoke-Native {
    param([scriptblock]$Command, [string]$What)
    & $Command
    if ($LASTEXITCODE -ne 0) {
        throw "$What exited with code $LASTEXITCODE"
    }
}

Set-Content -Path $Summary -Encoding utf8 -Value ("pipeline {0} root {1}" -f (Get-Date -Format o), $Root)

function Invoke-ProtocolStage {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    if (-not (Test-Path $vswhere)) {
        throw 'vswhere.exe was not found. Install Visual Studio 2022 with the C++ tools.'
    }
    $install = & $vswhere -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if (-not $install) {
        throw 'Visual C++ toolset was not found.'
    }
    $vcvars = Join-Path $install 'VC\Auxiliary\Build\vcvars64.bat'
    $exe = Join-Path $OutDir 'hl_selftest.exe'
    $cFile = Join-Path $Root 'protocol\host_link.c'
    $tFile = Join-Path $Root 'protocol\selftest.c'
    $objDir = Join-Path $OutDir 'protocol-obj'
    New-Item -ItemType Directory -Force -Path $objDir | Out-Null
    Push-Location $objDir
    try {
        $cmd = 'call "{0}" && cl /nologo /utf-8 /std:c11 /W3 /Fe:"{1}" "{2}" "{3}"' -f $vcvars, $exe, $cFile, $tFile
        cmd /c $cmd
        if ($LASTEXITCODE -ne 0) {
            throw "cl exited with code $LASTEXITCODE"
        }
    }
    finally {
        Pop-Location
    }
    & $exe
    if ($LASTEXITCODE -ne 0) {
        throw "protocol selftest exited with code $LASTEXITCODE"
    }
}

function Get-Lazbuild {
    if ($env:LAZBUILD -and (Test-Path $env:LAZBUILD)) {
        return $env:LAZBUILD
    }
    $found = Get-Command lazbuild -ErrorAction SilentlyContinue
    if ($found) {
        return $found.Source
    }
    $fallback = 'E:\lazarus\lazbuild.exe'
    if (Test-Path $fallback) {
        return $fallback
    }
    throw 'lazbuild was not found. Set LAZBUILD to lazbuild.exe.'
}

function Invoke-HostStage {
    $laz = Get-Lazbuild
    $lazarusRoot = Split-Path (Split-Path $laz -Parent) -Parent
    # E:\lazarus\lazbuild.exe -> parent is E:\lazarus, not a nested bin.
    if ((Split-Path $laz -Leaf) -eq 'lazbuild.exe') {
        $lazarusRoot = Split-Path $laz -Parent
    }
    $fpc = Join-Path $lazarusRoot 'fpc\3.2.2\bin\x86_64-win64\fpc.exe'
    if ($env:FPC -and (Test-Path $env:FPC)) {
        $fpc = $env:FPC
    }
    if (-not (Test-Path $fpc)) {
        throw "fpc.exe was not found at $fpc. Set FPC."
    }
    $lpi = Join-Path $Root 'PC_Host\pc_host.lpi'
    Invoke-Native -What 'lazbuild' -Command { & $laz --build-mode=Default $lpi }
    $hostOut = Join-Path $OutDir 'host'
    New-Item -ItemType Directory -Force -Path $hostOut | Out-Null
    $check = Join-Path $Root 'PC_Host\tools\hl_check.lpr'
    $unit = Join-Path $Root 'PC_Host\src'
    Push-Location $hostOut
    try {
        Invoke-Native -What 'fpc hl_check' -Command { & $fpc -Mobjfpc -Fu"$unit" -FE"$hostOut" $check }
    }
    finally {
        Pop-Location
    }
    $checkExe = Join-Path $hostOut 'hl_check.exe'
    Invoke-Native -What 'hl_check' -Command { & $checkExe }
}

function Get-GnuMake {
    if ($env:RENESAS_MAKE -and (Test-Path $env:RENESAS_MAKE)) {
        return $env:RENESAS_MAKE
    }
    $fallback = 'E:\e2_studio\eclipse\plugins\com.renesas.ide.exttools.gnumake.win32.x86_64_4.3.1.v20240909-0854\mk\make.exe'
    if (Test-Path $fallback) {
        return $fallback
    }
    throw 'GNU make from e2 studio was not found. Set RENESAS_MAKE. Embarcadero make cannot build these projects.'
}

function Invoke-FirmwareStage {
    $make = Get-GnuMake
    $configs = @('Debug', 'Release')
    if ($Configuration -ne 'both') {
        $configs = @($Configuration)
    }
    $projects = @(
        @{ Name = 'POC_RX71M'; Files = @('POC_RX71M.elf', 'POC_RX71M.mot', 'POC_RX71M.hex', 'POC_RX71M.bin') },
        @{ Name = 'POC_RA8P'; Files = @('POC_RA8P.elf', 'POC_RA8P.srec', 'POC_RA8P.hex', 'POC_RA8P.bin') }
    )
    $needle = $Root.ToLowerInvariant().Replace('/', '\')
    foreach ($project in $projects) {
        foreach ($config in $configs) {
            $dir = Join-Path $Root (Join-Path $project.Name $config)
            $makefile = Join-Path $dir 'makefile'
            $probe = Join-Path $dir 'src\board\subdir.mk'
            if (-not (Test-Path $makefile) -or -not (Test-Path $probe)) {
                throw "Missing e2 studio makefile under $($project.Name)\$config. Open the project in e2 studio once so Debug and Release makefiles are generated."
            }
            $text = [System.IO.File]::ReadAllText($probe).Replace('\\', '\').Replace('/', '\').ToLowerInvariant()
            if (-not $text.Contains($needle)) {
                throw "Makefile include paths do not match repo root $Root. Regenerate the e2 studio makefile at this location before CI."
            }
            Write-Host ("make {0} {1}" -f $project.Name, $config)
            Invoke-Native -What ("make {0} {1}" -f $project.Name, $config) -Command { & $make -C $dir -j 4 all }
            $dest = Join-Path $OutDir (Join-Path $project.Name $config)
            New-Item -ItemType Directory -Force -Path $dest | Out-Null
            foreach ($file in $project.Files) {
                $src = Join-Path $dir $file
                if (-not (Test-Path $src)) {
                    throw "Build finished but $src is missing."
                }
                Copy-Item -Force $src (Join-Path $dest $file)
            }
        }
    }
}

$plan = @()
if (Test-Selected 'protocol') { $plan += 'protocol' }
if (Test-Selected 'host') { $plan += 'host' }
if (Test-Selected 'firmware') { $plan += 'firmware' }

foreach ($name in $plan) {
    Write-Host "== $name =="
    try {
        switch ($name) {
            'protocol' { Invoke-ProtocolStage }
            'host' { Invoke-HostStage }
            'firmware' { Invoke-FirmwareStage }
        }
        Write-StageResult -Name $name -Status 'PASS' -Detail ''
    }
    catch {
        Write-StageResult -Name $name -Status 'FAIL' -Detail $_.Exception.Message
        $script:Failed = $true
        break
    }
}

if ($script:Failed) {
    exit 1
}
exit 0

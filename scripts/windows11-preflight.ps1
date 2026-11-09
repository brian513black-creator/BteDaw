param([Parameter(Mandatory=$true)][string]$ReportDirectory)
$ErrorActionPreference = 'Stop'

$os = Get-CimInstance Win32_OperatingSystem
if ($os.ProductType -ne 1 -or [int]$os.BuildNumber -lt 22000 -or $os.Caption -notmatch 'Windows 11') {
    throw 'This job requires Windows 11 desktop. A Windows Server runner does not qualify.'
}
if (-not [Environment]::Is64BitOperatingSystem -or -not [Environment]::Is64BitProcess) {
    throw 'Run the x64 GitHub runner with 64-bit PowerShell.'
}
$sessionId = [Diagnostics.Process]::GetCurrentProcess().SessionId
if ($sessionId -eq 0 -or -not [Environment]::UserInteractive) {
    throw 'Start the runner with .\run.cmd while signed in. GUI/audio checks cannot use a Windows service session.'
}

foreach ($tool in @('git','cmake','ctest','cpack')) {
    if (-not (Get-Command $tool -ErrorAction SilentlyContinue)) {
        throw "Install $tool and add it to PATH, then restart the runner terminal."
    }
}
$vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
if (-not (Test-Path $vswhere)) {
    throw 'Install Visual Studio 2022 with Desktop development with C++ and a Windows SDK.'
}
$visualStudio = & $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
if ($LASTEXITCODE -ne 0 -or -not $visualStudio) {
    throw 'Visual Studio C++ x64 build tools were not found.'
}
$sdkRoot = Join-Path ${env:ProgramFiles(x86)} 'Windows Kits/10/Include'
$sdkVersions = @(Get-ChildItem $sdkRoot -Directory -ErrorAction SilentlyContinue | Where-Object {
    Test-Path (Join-Path $_.FullName 'um/Windows.h')
} | Select-Object -ExpandProperty Name)
if ($sdkVersions.Count -eq 0) { throw 'Install a Windows SDK through Visual Studio Installer.' }

$nsis = Get-Command makensis -ErrorAction SilentlyContinue
$nsisDirectory = if ($nsis) { Split-Path $nsis.Source } else { Join-Path ${env:ProgramFiles(x86)} 'NSIS' }
if (-not (Test-Path (Join-Path $nsisDirectory 'makensis.exe'))) {
    throw 'Install NSIS from https://nsis.sourceforge.io/Download before running installer packaging.'
}

New-Item -ItemType Directory -Path $ReportDirectory -Force | Out-Null
$report = [ordered]@{
    capturedAtUtc = [DateTime]::UtcNow.ToString('o')
    os = [ordered]@{ caption=$os.Caption; version=$os.Version; build=$os.BuildNumber; productType=$os.ProductType }
    interactiveSessionId = $sessionId
    buildTools = [ordered]@{ visualStudio=$visualStudio; windowsSDKs=$sdkVersions; nsisDirectory=$nsisDirectory }
    audioDevices = @(Get-CimInstance Win32_SoundDevice | Select-Object Name,Manufacturer,Status)
    validationScope = 'OS/tool preflight only. Device enumeration does not establish recording, latency or plugin compatibility.'
}
$report | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $ReportDirectory 'windows11-preflight.json') -Encoding UTF8
Write-Output "Windows 11 x64 build $($os.BuildNumber), interactive session $sessionId. Preflight passed."

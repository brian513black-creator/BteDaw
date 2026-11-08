param([Parameter(Mandatory=$true)][string]$Executable)
$ErrorActionPreference = 'Stop'
$path = (Resolve-Path $Executable).Path
$process = Start-Process -FilePath $path -ArgumentList '--smoke-test' -PassThru
if (-not $process.WaitForExit(30000)) {
    $process.Kill()
    throw 'The Windows application did not complete its startup smoke check within 30 seconds.'
}
if ($process.ExitCode -ne 0) { throw "Windows startup failed with exit code $($process.ExitCode)." }
Write-Output 'Windows application initialized its engine and interface, then exited cleanly.'

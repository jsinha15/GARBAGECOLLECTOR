# One-click launcher: builds HeapLens if needed, starts the server and opens the browser.
#   .\start.ps1                 (or double-click start.bat)
#   .\start.ps1 -Port 9000      use another port
#   .\start.ps1 -Rebuild        force a rebuild first
param([int]$Port = 8080, [switch]$Rebuild, [string]$Gcc = "")

$ErrorActionPreference = "Stop"
Set-Location $PSScriptRoot
if ($Gcc -ne "") { $env:Path = "$Gcc;$env:Path" }
elseif (-not (Get-Command g++ -ErrorAction SilentlyContinue) -and (Test-Path "C:\mingw64\bin\g++.exe")) { $env:Path = "C:\mingw64\bin;$env:Path" }

# rebuild when the exe is missing or older than any source file
$exe = "build\minigc.exe"
$stale = $true
if ((Test-Path $exe) -and -not $Rebuild) {
    $newest = Get-ChildItem src, include -Recurse -File | Sort-Object LastWriteTime -Descending | Select-Object -First 1
    $stale = $newest.LastWriteTime -gt (Get-Item $exe).LastWriteTime
}
if ($stale) {
    Write-Host "Building HeapLens (first run or sources changed) ..." -ForegroundColor Cyan
    & "$PSScriptRoot\build_mingw.ps1" | Out-Host
    if (-not (Test-Path $exe)) { throw "Build failed - see the messages above." }
}

# free the port if an old server is still running
Get-NetTCPConnection -LocalPort $Port -State Listen -ErrorAction SilentlyContinue | ForEach-Object {
    Stop-Process -Id $_.OwningProcess -Force -ErrorAction SilentlyContinue
}

$url = "http://127.0.0.1:$Port/"
Write-Host "Starting HeapLens on $url   (close this window or press Ctrl+C to stop)" -ForegroundColor Green
Start-Job -ScriptBlock { param($u) Start-Sleep 2; Start-Process $u } -ArgumentList $url | Out-Null
& .\build\minigc.exe --serve --port $Port

# Build HeapLens with MinGW-w64 GCC from plain PowerShell (no bash, no CMake needed).
#   .\build_mingw.ps1                     builds build\minigc.exe and build\minigc_tests.exe
#   .\build_mingw.ps1 -Run                ...and starts the UI server
#   .\build_mingw.ps1 -Gcc C:\mingw64\bin   if g++ is not already on PATH
param([string]$Gcc = "", [switch]$Run)

$ErrorActionPreference = "Stop"
if ($Gcc -ne "") { $env:Path = "$Gcc;$env:Path" }
elseif (-not (Get-Command g++ -ErrorAction SilentlyContinue) -and (Test-Path "C:\mingw64\bin\g++.exe")) { $env:Path = "C:\mingw64\bin;$env:Path" }
if (-not (Get-Command g++ -ErrorAction SilentlyContinue)) { throw "g++ not found. Install MinGW-w64 (e.g. winlibs.com) or pass -Gcc <path-to-bin>." }

Set-Location $PSScriptRoot
New-Item -ItemType Directory -Force build | Out-Null
$core  = @(Get-ChildItem src\core\*.cpp | ForEach-Object { $_.FullName })
$flags = @("-std=c++17", "-O1", "-fno-strict-aliasing", "-Wall", "-Wextra", "-Iinclude")

Write-Host "Compiling minigc.exe ..."
& g++ @flags ($core + "$PWD\src\main.cpp" + "$PWD\src\server.cpp") -o build\minigc.exe -lws2_32
if ($LASTEXITCODE -ne 0) { throw "compile failed" }

Write-Host "Compiling minigc_tests.exe ..."
$tests = @(Get-ChildItem tests\*.cpp | ForEach-Object { $_.FullName })
& g++ @flags -Itests ($core + $tests) -o build\minigc_tests.exe
if ($LASTEXITCODE -ne 0) { throw "test compile failed" }

Write-Host "`nBuilt build\minigc.exe and build\minigc_tests.exe"
& .\build\minigc_tests.exe | Select-Object -Last 1
if ($Run) { & .\build\minigc.exe --serve } else { Write-Host "Start the UI:  .\build\minigc.exe --serve   then open http://127.0.0.1:8080/" }

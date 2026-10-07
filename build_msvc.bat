@echo off
rem Build HeapLens with plain MSVC (no CMake needed).
rem Usage:  build_msvc.bat            builds build\minigc.exe and build\minigc_tests.exe
rem         build_msvc.bat test       ...and runs the tests
setlocal
set ROOT=%~dp0
cd /d "%ROOT%"

if not "%VSCMD_VER%"=="" goto have_vs
set VCVARS=
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Professional\VC\Auxiliary\Build\vcvars64.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
if exist "%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" set "VCVARS=%ProgramFiles%\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if "%VCVARS%"=="" goto no_vs
call "%VCVARS%" >nul
:have_vs

if not exist build mkdir build
if not exist build\obj mkdir build\obj
if not exist build\obj_app mkdir build\obj_app
if not exist build\obj_test mkdir build\obj_test

set CXXFLAGS=/nologo /std:c++17 /EHsc /W3 /Od /Zi /Iinclude /Itests /utf-8
cl %CXXFLAGS% /c /Fobuild\obj\ src\core\*.cpp || exit /b 1
lib /nologo /OUT:build\minigc_core.lib build\obj\*.obj || exit /b 1

set UIDIR=%ROOT:\=/%ui
cl %CXXFLAGS% /DMINIGC_UI_DIR=\"%UIDIR%\" /Fe:build\minigc.exe /Fobuild\obj_app\ /Fd:build\minigc.pdb src\main.cpp src\server.cpp build\minigc_core.lib ws2_32.lib || exit /b 1
cl %CXXFLAGS% /Fe:build\minigc_tests.exe /Fobuild\obj_test\ /Fd:build\minigc_tests.pdb tests\*.cpp build\minigc_core.lib || exit /b 1

echo.
echo Built build\minigc.exe and build\minigc_tests.exe
if /i "%1"=="test" build\minigc_tests.exe
goto :eof

:no_vs
echo Could not find Visual Studio 2022 C++ tools. Open an "x64 Native Tools Command Prompt for VS 2022" and rerun.
exit /b 1

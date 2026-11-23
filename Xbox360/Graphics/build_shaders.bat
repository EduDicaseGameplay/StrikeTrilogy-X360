@echo off
rem Compiles present.hlsl with the XDK's fxc into present_vs.h / present_ps.h (Xenos microcode as byte arrays).
setlocal
set "FXC=%XEDK%\bin\win32\fxc.exe"
if not exist "%FXC%" set "FXC=C:\Program Files (x86)\Microsoft Xbox 360 SDK\bin\win32\fxc.exe"
"%FXC%" /nologo /T vs_3_0 /E VsMain /Vn g_presentVs /Fh "%~dp0present_vs.h" "%~dp0present.hlsl" || exit /b 1
"%FXC%" /nologo /T ps_3_0 /E PsMain /Vn g_presentPs /Fh "%~dp0present_ps.h" "%~dp0present.hlsl" || exit /b 1

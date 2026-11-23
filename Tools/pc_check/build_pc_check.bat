@echo off
rem Builds Tools\pc_check\build\pc_check.exe: the port's sound and render modules on PC (clang-cl,
rem Visual Studio 2022 Build Tools). Needs Tools\pcbuild.bat first (it builds ds_core.lib).
setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%
set ROOT=%~dp0..\..
set REF=%ROOT%\Reference\StrikeHDCollection
set X=%ROOT%\Xbox360
set OUT=%~dp0build
if not exist "%OUT%" mkdir "%OUT%"
clang-cl /nologo /O2 /W0 /MD /D_CRT_SECURE_NO_WARNINGS /std:c++17 ^
  /I"%REF%\src" /I"%REF%\src\hw" /I"%REF%\src\runtime" /I"%REF%\games" /I"%REF%\third_party\z80" ^
  /I"%REF%\third_party\ymfm\src" /I"%REF%\third_party\stb" /I"%X%\Audio" /I"%X%\Graphics" /I"%ROOT%\Source\Shared" ^
  "%~dp0pc_check.c" "%REF%\src\hw\genesis.c" "%REF%\third_party\z80\z80.c" ^
  "%REF%\third_party\ymfm\src\ymfm_opn.cpp" "%REF%\third_party\ymfm\src\ymfm_adpcm.cpp" "%REF%\third_party\ymfm\src\ymfm_ssg.cpp" ^
  "%X%\Audio\strike_sound_x360.c" "%X%\Audio\ym2612_x360.cpp" "%X%\Graphics\strike_render.c" "%X%\Graphics\vdp_render_snap.c" "%X%\Graphics\strike_osd.c" "%X%\Graphics\strike_vdp_fast.c" "%ROOT%\Source\Shared\strike_features.c" ^
  "%REF%\build\pc\ds_core.lib" /Fo"%OUT%\\" /Fe"%OUT%\pc_check.exe" || exit /b 1
echo built %OUT%\pc_check.exe

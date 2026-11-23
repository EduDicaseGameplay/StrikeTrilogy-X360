@echo off
rem Builds the reference's headless test programs on PC (no SDL, no host):
rem   ds_test = recompiled games, ds_ref = Musashi 68000 reference (lockstep).
rem Visual Studio 2022 Build Tools (clang, CMake, Ninja). Run tools\gen_all.bat first.
setlocal
set VS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools
call "%VS%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1
set PATH=%VS%\VC\Tools\Llvm\x64\bin;%PATH%
cd /d "%~dp0..\Reference\StrikeHDCollection" || exit /b 1
cmake -S . -B build/pc -G Ninja -DDS_BUILD_HOST=OFF -DDS_BUILD_TESTS=ON -DCMAKE_C_COMPILER=clang-cl -DCMAKE_CXX_COMPILER=clang-cl -DCMAKE_BUILD_TYPE=Release  || exit /b 1
cmake --build build/pc

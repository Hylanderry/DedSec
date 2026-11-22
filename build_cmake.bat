@echo off
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64
cd /d "D:\Port GTA VI\AI\dedsec"
cmake -S runtime -B runtime/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
ninja -C runtime/build
if %ERRORLEVEL% NEQ 0 exit /b %ERRORLEVEL%
echo BUILD_SUCCESS
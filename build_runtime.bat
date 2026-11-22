@echo off
set LOGFILE=D:\Port GTA VI\AI\dedsec\build_log.txt
echo === BUILD START %DATE% %TIME% === > "%LOGFILE%" 2>&1
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvarsall.bat" x64 >> "%LOGFILE%" 2>&1
cd /d "D:\Port GTA VI\AI\dedsec"
echo === CMAKE CONFIGURE === >> "%LOGFILE%" 2>&1
cmake -S runtime -B runtime/build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo >> "%LOGFILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo CMAKE_CONFIG_FAILED >> "%LOGFILE%" 2>&1
    exit /b %ERRORLEVEL%
)
echo === NINJA BUILD === >> "%LOGFILE%" 2>&1
ninja -C runtime/build >> "%LOGFILE%" 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo BUILD_FAILED >> "%LOGFILE%" 2>&1
    exit /b %ERRORLEVEL%
)
echo BUILD_SUCCESS >> "%LOGFILE%" 2>&1
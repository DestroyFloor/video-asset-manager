@echo off
setlocal
cd /d "%~dp0"
if errorlevel 1 cd /d "D:\python_file\CB_web\workbench_qt"

set "PATH=C:\Qt\Tools\mingw1310_64\bin;C:\Qt\Tools\Ninja;C:\Qt\6.11.2\mingw_64\bin;%PATH%"
set "CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
set "BUILD=build2"

echo ==================================================
echo  [1/3] Configure CMake  (Ninja + MinGW + Qt 6.11)
echo ==================================================
"%CMAKE%" -S . -B "%BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/mingw_64 -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++
if errorlevel 1 goto fail

echo.
echo ==================================================
echo  [2/3] Build
echo ==================================================
"%CMAKE%" --build "%BUILD%"
if errorlevel 1 goto fail

echo.
echo ==================================================
echo  [3/3] Launch  %BUILD%\workbench_qt.exe
echo ==================================================
start "" "%BUILD%\workbench_qt.exe"
echo.
echo Program started. If no window appeared, copy ALL text above.
pause
goto :eof

:fail
echo.
echo *** BUILD FAILED ***
echo Copy ALL text above and send it to the assistant.
pause

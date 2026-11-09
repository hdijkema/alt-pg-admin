@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
"C:\Qt\Tools\CMake_64\bin\cmake.exe" -S "%~dp0." -B "%~dp0build\Release" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.1/msvc2022_64 -DCMAKE_MAKE_PROGRAM=C:/Qt/Tools/Ninja/ninja.exe %*
if errorlevel 1 exit /b 1
"C:\Qt\Tools\CMake_64\bin\cmake.exe" --build "%~dp0build\Release"
if errorlevel 1 exit /b 1
"C:\Qt\6.11.1\msvc2022_64\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "%~dp0build\Release\pgadmin_qt.exe"
exit /b %errorlevel%

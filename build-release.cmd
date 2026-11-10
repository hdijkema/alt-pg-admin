@echo off
setlocal
rem Local defaults can be overridden by the GitHub Actions job.
if not defined PGADM_QT_ROOT set "PGADM_QT_ROOT=C:\Qt\6.11.1\msvc2022_64"
if not defined PGADM_CMAKE set "PGADM_CMAKE=C:\Qt\Tools\CMake_64\bin\cmake.exe"
if not exist "%PGADM_CMAKE%" set "PGADM_CMAKE=cmake"
if not defined PGADM_NINJA set "PGADM_NINJA=C:\Qt\Tools\Ninja\ninja.exe"
if not exist "%PGADM_NINJA%" set "PGADM_NINJA=ninja"
if not defined PGADM_VS_ROOT (
    for /f "usebackq delims=" %%I in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "PGADM_VS_ROOT=%%I"
)
if not defined PGADM_VS_ROOT (
    echo Visual Studio with the C++ x64 tools is required.
    exit /b 1
)
call "%PGADM_VS_ROOT%\VC\Auxiliary\Build\vcvars64.bat"
if errorlevel 1 exit /b 1
"%PGADM_CMAKE%" -S "%~dp0." -B "%~dp0build\Release" -G Ninja -DCMAKE_BUILD_TYPE=Release "-DCMAKE_PREFIX_PATH=%PGADM_QT_ROOT%" "-DCMAKE_MAKE_PROGRAM=%PGADM_NINJA%" %*
if errorlevel 1 exit /b 1
"%PGADM_CMAKE%" --build "%~dp0build\Release"
if errorlevel 1 exit /b 1
"%PGADM_QT_ROOT%\bin\windeployqt.exe" --release --no-translations --no-opengl-sw --no-system-d3d-compiler "%~dp0build\Release\pgadmin_qt.exe"
exit /b %errorlevel%

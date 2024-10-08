setlocal

SET MAYA_VERSION=2025
REM "vs" "ninja"
REM use VS for the debugger, otherwise use NINJA
REM Until I figure out how to debug using nvim
SET BACKEND=vs
REM "debug" "debugoptimized" "release"
SET BUILDTYPE=debugoptimized
SET BUILDDIR=mayabuild_%BUILDTYPE%_%MAYA_VERSION%_%BACKEND%

SET DK=D:\Users\Tyler\src\MayaDevkits\Autodesk_Maya_2025_2_Update_DEVKIT_Windows\devkitBase

if not exist %BUILDDIR%\ (
    meson setup %BUILDDIR% ^
        -Dmaya:maya_version=%MAYA_VERSION% ^
        -Dmaya:maya_devkit_base=%DK% ^
        --buildtype %BUILDTYPE% ^
        --backend %BACKEND% ^
        --vsenv
)

if exist %BUILDDIR%\ (
    meson compile -C %BUILDDIR%
    meson install -C %BUILDDIR%
)

pause

setlocal

SET MAYA_VERSION=2024
SET BUILD=mayabuild_%MAYA_VERSION%
SET COMPILER=Visual Studio 15 2017 Win64

REM Make sure to update the remote repos before building
REM You can just comment this out if you want
REM git submodule update --remote --recursive

cmake -B ./build -DMAYA_VERSION="%MAYA_VERSION%" -G "%COMPILER%"
cmake --build ./build --config Release

pause

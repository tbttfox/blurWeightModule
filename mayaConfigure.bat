setlocal

SET MAYA_VERSION=2023
SET BUILD=mayabuild_%MAYA_VERSION%
SET COMPILER=Visual Studio 16 2019

REM Make sure to update the remote repos before building
REM You can just comment this out if you want
REM git submodule update --remote --recursive

cmake -B ./%BUILD% -DMAYA_VERSION="%MAYA_VERSION%" -G "%COMPILER%"
cmake --build ./%BUILD% --config Release

pause

@echo off
setlocal EnableExtensions

set ROOT=%~dp0
set BUILD_DIR=%ROOT%build\windows-debug

if not exist %BUILD_DIR% (
    mkdir %BUILD_DIR%
)

cmake -S %ROOT% -B %BUILD_DIR% -DSLAYER3D_BUILD_TESTS=ON -DSLAYER3D_BUILD_DEMOS=ON -DSLAYER3D_BUILD_BENCHMARKS=ON
if errorlevel 1 exit /b %errorlevel%

cmake --build %BUILD_DIR% --config Debug --target ALL_BUILD
exit /b %errorlevel%

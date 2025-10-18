@echo off
cd /D "%~dp0"
set STRIPACK_LIB=ThirdParty\stripack\win64\stripack.lib
cl test_10k_standalone.cpp %STRIPACK_LIB% /Fe:test_10k_standalone.exe /link /SUBSYSTEM:CONSOLE
if %ERRORLEVEL% EQU 0 (
    echo.
    echo Build successful! Running test...
    echo.
    test_10k_standalone.exe
) else (
    echo Build failed!
    exit /b 1
)

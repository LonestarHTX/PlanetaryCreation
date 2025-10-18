@echo off
REM Build Geogram static library for Win64 (Unreal Engine integration)
REM This script uses CMake and Visual Studio to build a Release static library

setlocal

set SCRIPT_DIR=%~dp0
set SOURCE_DIR=%SCRIPT_DIR%geogram-src\geogram_1.9.7
set BUILD_DIR=%SCRIPT_DIR%build-win64
set OUTPUT_DIR=%SCRIPT_DIR%lib\Win64

echo ========================================
echo Geogram Win64 Static Library Builder
echo ========================================
echo.
echo Source:  %SOURCE_DIR%
echo Build:   %BUILD_DIR%
echo Output:  %OUTPUT_DIR%
echo.

if not exist "%SOURCE_DIR%\CMakeLists.txt" (
    echo ERROR: Source directory not found or missing CMakeLists.txt
    echo Expected: %SOURCE_DIR%\CMakeLists.txt
    exit /b 1
)

REM Create output directory
if not exist "%OUTPUT_DIR%" mkdir "%OUTPUT_DIR%"

REM Create build directory
if not exist "%BUILD_DIR%" mkdir "%BUILD_DIR%"

pushd "%BUILD_DIR%"

echo.
echo [1/3] Configuring CMake...
echo.

cmake "%SOURCE_DIR%" ^
    -G "Visual Studio 17 2022" ^
    -A x64 ^
    -DCMAKE_BUILD_TYPE=Release ^
    -DVORPALINE_BUILD_DYNAMIC=OFF ^
    -DGEOGRAM_WITH_GRAPHICS=OFF ^
    -DGEOGRAM_WITH_EXPLORAGRAM=OFF ^
    -DGEOGRAM_WITH_LUA=OFF ^
    -DGEOGRAM_LIB_ONLY=ON ^
    -DGEOGRAM_USE_SYSTEM_GLFW3=OFF

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: CMake configuration failed
    popd
    exit /b 1
)

echo.
echo [2/3] Building Release configuration...
echo.

cmake --build . --config Release --target geogram

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo ERROR: Build failed
    popd
    exit /b 1
)

echo.
echo [3/3] Copying library to output directory...
echo.

REM Find and copy the library file
if exist "lib\Release\geogram.lib" (
    copy /Y "lib\Release\geogram.lib" "%OUTPUT_DIR%\geogram.lib"
    echo SUCCESS: Copied geogram.lib
) else if exist "Release\geogram.lib" (
    copy /Y "Release\geogram.lib" "%OUTPUT_DIR%\geogram.lib"
    echo SUCCESS: Copied geogram.lib
) else if exist "geogram\Release\geogram.lib" (
    copy /Y "geogram\Release\geogram.lib" "%OUTPUT_DIR%\geogram.lib"
    echo SUCCESS: Copied geogram.lib
) else (
    echo ERROR: Could not find geogram.lib in expected locations
    echo Searched:
    echo   - lib\Release\geogram.lib
    echo   - Release\geogram.lib
    echo   - geogram\Release\geogram.lib
    popd
    exit /b 1
)

popd

echo.
echo ========================================
echo Build Complete!
echo ========================================
echo.
echo Library installed to: %OUTPUT_DIR%\geogram.lib
echo.
echo Next steps:
echo 1. Rebuild your Unreal project to enable WITH_GEOGRAM=1
echo 2. Set r.PaperTriangulation.Backend to "Geogram" in the console
echo 3. Run your triangulation tests
echo.

endlocal


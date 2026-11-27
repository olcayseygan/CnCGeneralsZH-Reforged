@echo off
setlocal enabledelayedexpansion

rem Build C&C Generals Zero Hour, x64 (or ARM64: PLATFORM below). Double-click it, or:
rem
rem   build.bat                    configure (if needed) + build Release
rem   build.bat Debug              build another config (Release|RelWithDebInfo|Debug)
rem   build.bat Release test       build, then run ctest
rem   build.bat Release generals   build a single target
rem   build.bat clean              delete the build tree and configure from scratch
rem
rem Nothing to edit before the first run: it finds cmake itself, fetches the third-party
rem sources EA stripped and the fork's own art, builds, and copies the exe and everything
rem beside it into GeneralsMD\Run. What it cannot fetch is the game: a Zero Hour install's
rem *.big go next to generals.exe in GeneralsMD\Run, and the base game's in Run\ZH_Generals.
rem
rem The knobs below are overrides and every one of them empty is the supported path. A machine that
rem does need one puts its own "set" lines in build.local.bat beside this file, which is
rem git-ignored and read right after these defaults, rather than editing a tracked file.

set "CMAKE="
set "VS_EDITIONS=Community Professional Enterprise BuildTools"
set "GENERATOR=Visual Studio 17 2022"
set "DEFAULT_CONFIG=Release"
set "BUILD="
rem x64, or ARM64 for Windows on Arm.  An ARM64 machine builds ARM64, into build-arm64 unless BUILD says
rem otherwise; build.local.bat can still ask it for x64.
set "PLATFORM=x64"
if /i "%PROCESSOR_ARCHITECTURE%"=="ARM64" set "PLATFORM=ARM64"

if exist "%~dp0build.local.bat" (
    echo [build] reading build.local.bat
    call "%~dp0build.local.bat"
)
rem A caller that names the platform wins over both: windows-ci.ps1 -Platform sets ZH_PLATFORM (x64 or
rem ARM64), so an ARM64 machine can build and test the x64 game as a second CI lane.
if defined ZH_PLATFORM set "PLATFORM=%ZH_PLATFORM%"

rem Double-clicked from Explorer, the window closes on the last line and nobody reads it.
rem A double-click passes no arguments and puts this file's own name in the parent command line.
set "PAUSE_AT_END="
if "%~1"=="" echo %cmdcmdline% | find /i "%~nx0" >nul && set "PAUSE_AT_END=1"

set "ROOT=%~dp0"
set "SRC=%ROOT%GeneralsMD\Code"
if not defined BUILD if /i "%PLATFORM%"=="ARM64" set "BUILD=%ROOT%build-arm64"
if not defined BUILD set "BUILD=%ROOT%build64"
set "CONFIG=%~1"
set "ARG2=%~2"

if /i "%CONFIG%"=="clean" (
    echo [build] removing %BUILD%
    if exist "%BUILD%" rmdir /s /q "%BUILD%"
    set "CONFIG=%ARG2%"
    set "ARG2="
)
if "%CONFIG%"=="" set "CONFIG=%DEFAULT_CONFIG%"

set "RUNTESTS="
set "TARGET="
if /i "%ARG2%"=="test" (set "RUNTESTS=1") else (if not "%ARG2%"=="" set "TARGET=%ARG2%")

rem --- locate cmake: the override, then PATH, then the copy shipped with VS2022 ---
if defined CMAKE if not exist "%CMAKE%" (
    echo [build] ERROR: CMAKE is set to "%CMAKE%" at the top of this file, but that file does not exist.
    goto :fail
)
for /f "delims=" %%C in ('where cmake 2^>nul') do if not defined CMAKE set "CMAKE=%%C"
if not defined CMAKE (
    for %%E in (%VS_EDITIONS%) do (
        set "TRY=C:\Program Files\Microsoft Visual Studio\2022\%%E\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
        if not defined CMAKE if exist "!TRY!" set "CMAKE=!TRY!"
    )
)
if not defined CMAKE (
    echo [build] ERROR: cmake.exe not found on PATH or under Visual Studio 2022.
    echo [build] Install Visual Studio 2022 with the C++ desktop workload and run this again.
    goto :fail
)
echo [build] cmake:  %CMAKE%
echo [build] config: %CONFIG%

rem --- third-party sources the repository does not carry (zlib, LZH-Light, the DirectX 8 headers,
rem the GameSpy SDK) and the upscaled art. Fetches whatever is missing and is a no-op once it is
rem there, so one build.bat on a fresh clone is enough. ---
powershell -NoProfile -ExecutionPolicy Bypass -File "%SRC%\Tools\vendor.ps1"
if !errorlevel! neq 0 (
    echo [build] ERROR: fetching the third-party sources failed.
    goto :fail
)

rem --- configure (only when the cache is missing) ---
if not exist "%BUILD%\CMakeCache.txt" (
    echo [build] configuring %SRC% -^> %BUILD%
    "%CMAKE%" -S "%SRC%" -B "%BUILD%" -G "%GENERATOR%" -A %PLATFORM%
    if !errorlevel! neq 0 (
        echo [build] ERROR: configure failed.
        goto :fail
    )
)

rem --- a source written while the compiler runs leaves an exe of two versions: the objects built before
rem the write saw the old header, the ones after it the new, and a class laid out two ways reads one
rem member through another. On 2026-11-27 ControlBar.h lost its uncommitted members at 09:13 in a
rem 09:12-09:14 build, and the bar's background draw read the scheme pointer 0x128 bytes short of where
rem ControlBar.cpp kept it. The build notes when it starts and refuses the result if a source under
rem Code was written after; the next run recompiles exactly those, so running it again is the fix. ---
set "BUILD_START="
for /f %%T in ('powershell -NoProfile -Command "[DateTime]::UtcNow.Ticks"') do set "BUILD_START=%%T"

rem --- build ---
if defined TARGET (
    echo [build] building target %TARGET%
    "%CMAKE%" --build "%BUILD%" --config %CONFIG% --target %TARGET%
) else (
    "%CMAKE%" --build "%BUILD%" --config %CONFIG%
)
if !errorlevel! neq 0 (
    echo [build] ERROR: build failed.
    goto :fail
)
powershell -NoProfile -Command "if (-not $env:BUILD_START) { exit 0 }; $t = [long]$env:BUILD_START; $c = @(Get-ChildItem -LiteralPath $env:SRC -Recurse -File -ErrorAction SilentlyContinue | Where-Object { $_.Extension -match '^\.(h|hpp|inl|inc|c|cpp)$' -and $_.LastWriteTimeUtc.Ticks -gt $t }); if ($c.Count -eq 0) { exit 0 }; $c | Select-Object -First 20 | ForEach-Object { '[build]   ' + $_.FullName }; exit 1"
if !errorlevel! neq 0 (
    echo [build] ERROR: the sources above were written during the build, so its objects mix two versions.
    echo [build] Run build.bat again once nobody is editing them; it recompiles what changed.
    goto :fail
)

rem --- tests ---
if defined RUNTESTS (
    rem ctest lives beside cmake. It is not on PATH when cmake came from the VS2022 install,
    rem and "cmake -E chdir ctest" then fails with a bare "no such file or directory".
    for %%I in ("%CMAKE%") do set "CTEST=%%~dpIctest.exe"
    echo [build] running !CTEST!
    "!CTEST!" --test-dir "%BUILD%" -C %CONFIG% --output-on-failure
    if !errorlevel! neq 0 (
        echo [build] ERROR: tests failed.
        goto :fail
    )
)

echo [build] done. %ROOT%GeneralsMD\Run\generals.exe is the one to run.
if defined PAUSE_AT_END pause
endlocal
exit /b 0

:fail
if defined PAUSE_AT_END pause
endlocal
exit /b 1

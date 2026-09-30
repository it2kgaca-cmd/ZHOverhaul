@echo off
setlocal EnableExtensions EnableDelayedExpansion

rem Install only the INI files changed by ZHOverhaul since the stock Zero Hour
rem gameplay-data import. Do NOT copy the entire GameData\INI source tree into
rem the game installation: stock BIG archives continue to provide unchanged data.

set "BASE_DATA_COMMIT=6199e32a7750a6f4394aab02c2fd8b23c010e522"
set "REPO_ROOT=%~dp0.."

if "%~1"=="" (
    echo Usage:
    echo   %~nx0 "C:\Path\To\Command and Conquer Generals Zero Hour"
    echo.
    echo The path must contain a Data directory.
    exit /b 2
)

set "GAME_ROOT=%~1"
if not exist "%GAME_ROOT%\Data" (
    echo ERROR: "%GAME_ROOT%\Data" was not found.
    exit /b 2
)

pushd "%REPO_ROOT%" >nul
if errorlevel 1 (
    echo ERROR: Could not enter repository root "%REPO_ROOT%".
    exit /b 2
)

git rev-parse --is-inside-work-tree >nul 2>&1
if errorlevel 1 (
    echo ERROR: "%REPO_ROOT%" is not a Git working tree.
    popd >nul
    exit /b 2
)

set /a COPIED=0
for /f "usebackq delims=" %%F in (`git diff --diff-filter=ACMRT --name-only "%BASE_DATA_COMMIT%" HEAD -- "GameData/INI"`) do (
    set "SRC_REL=%%F"
    set "SRC_REL=!SRC_REL:/=\!"
    set "SRC=%REPO_ROOT%\!SRC_REL!"
    set "REL=!SRC_REL:GameData\=!"
    set "DST=%GAME_ROOT%\Data\!REL!"

    if not exist "!SRC!" (
        echo ERROR: Source INI was not found:
        echo   !SRC!
        popd >nul
        exit /b 1
    )

    for %%D in ("!DST!") do set "DST_DIR=%%~dpD"
    if not exist "!DST_DIR!" (
        mkdir "!DST_DIR!" >nul 2>&1
        if errorlevel 1 (
            echo ERROR: Could not create destination directory:
            echo   !DST_DIR!
            popd >nul
            exit /b 1
        )
    )

    copy /Y "!SRC!" "!DST!" >nul
    if errorlevel 1 (
        echo ERROR copying:
        echo   FROM: !SRC!
        echo     TO: !DST!
        popd >nul
        exit /b 1
    )

    echo Installed !REL!
    set /a COPIED+=1
)

popd >nul

echo.
echo Installed !COPIED! ZHOverhaul INI override file(s).
echo Stock data continues to come from the game's BIG archives.
exit /b 0

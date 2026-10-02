@echo off
REM ============================================================
REM  AlienShooterCHS build script (Release|Win32, MSVC v141)
REM  usage: build.bat
REM  output: dist\AlienShooterCHS.dll
REM
REM  Freestanding: no CRT link (DllMain must not depend on CRT
REM  init, and the /MT entry symbol decoration breaks the link).
REM  Custom entry point CHS_DllEntry is exported from dllmain.cpp.
REM
REM  INCLUDE/LIB are set explicitly instead of relying on
REM  vcvars32.bat, because the build runs in a restricted sandbox.
REM ============================================================
setlocal

set "VCTOOLS=C:\Program Files (x86)\Microsoft Visual Studio\2017\Professional\VC\Tools\MSVC\14.16.27023"
set "SDKROOT=C:\Program Files (x86)\Windows Kits\10"
set "SDKVER=10.0.26100.0"

if not exist "%VCTOOLS%\bin\Hostx86\x86\cl.exe" (
    echo [ERROR] cl.exe not found
    exit /b 1
)
if not exist "%SDKROOT%\Include\%SDKVER%\um\Windows.h" (
    echo [ERROR] Windows SDK %SDKVER% not found
    exit /b 1
)

set "INCLUDE=%VCTOOLS%\include;%SDKROOT%\Include\%SDKVER%\um;%SDKROOT%\Include\%SDKVER%\shared;%SDKROOT%\Include\%SDKVER%\ucrt"
set "LIB=%VCTOOLS%\lib\x86;%SDKROOT%\Lib\%SDKVER%\um\x86;%SDKROOT%\Lib\%SDKVER%\ucrt\x86"
set "PATH=%VCTOOLS%\bin\Hostx86\x86;%PATH%"

set "ROOT=%~dp0"
set "OUT=%ROOT%dist"
if not exist "%OUT%" mkdir "%OUT%"

echo [BUILD] Release/Win32 MSVC 14.16 / SDK %SDKVER%
cl.exe /nologo /c /O2 /MT /GS- /W3 /EHsc /D_CRT_SECURE_NO_WARNINGS /DNDEBUG /DWIN32 /D_WINDOWS /utf-8 /I"%ROOT%AlienShooterCHS" /Fo:"%OUT%\\" "%ROOT%AlienShooterCHS\dllmain.cpp" "%ROOT%AlienShooterCHS\hook.cpp" "%ROOT%AlienShooterCHS\pch.cpp"
if errorlevel 1 (
    echo [ERROR] compile failed
    exit /b 1
)

echo [LINK] AlienShooterCHS.dll
REM msvcrt.lib is added ONLY for the __except_handler3 import (SEH support).
REM We still do not link or initialise the CRT: /NODEFAULTLIB stays, and the
REM entry point is our own CHS_DllEntry. msvcrt.dll is always already loaded.
REM Do NOT replace this with a full CRT link -- DllMain would then depend on
REM CRT init that never runs for an ASI injected this early.
link.exe /nologo /DLL /OUT:"%OUT%\AlienShooterCHS.dll" /MAP:"%OUT%\AlienShooterCHS.map" /ENTRY:CHS_DllEntry /SUBSYSTEM:WINDOWS /NODEFAULTLIB /IGNORE:4078 "%OUT%\dllmain.obj" "%OUT%\hook.obj" "%OUT%\pch.obj" /LIBPATH:"%VCTOOLS%\lib\x86" /LIBPATH:"%SDKROOT%\Lib\%SDKVER%\um\x86" kernel32.lib user32.lib gdi32.lib msvcrt.lib
if errorlevel 1 (
    echo [ERROR] link failed
    exit /b 1
)

echo [DONE] %OUT%\AlienShooterCHS.dll
endlocal

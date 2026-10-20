@echo off
rem bootstrap.cmd <output directory>: builds nulldc3-winrun.exe from winrun.cpp of this directory with cl.exe alone,
rem before the tests project (which builds it as well) can be built and run inside it. Takes the developer
rem environment of the newest Visual Studio (vswhere -latest) when cl.exe is not on the PATH. Writes into the output
rem directory only (the objects and temporaries of cl.exe go there too).
setlocal
if "%~1"=="" (echo usage: bootstrap.cmd ^<output directory^> & exit /b 2)
set "OUT=%~f1"
set "SRC=%~dp0winrun.cpp"
set VSCMD_SKIP_SENDTELEMETRY=1
where cl.exe >nul 2>nul
if errorlevel 1 (
  for /f "usebackq delims=" %%i in (`"%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
)
if defined VSDIR call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul
where cl.exe >nul 2>nul
if errorlevel 1 (echo bootstrap.cmd: no cl.exe: run it from a developer prompt of Visual Studio & exit /b 1)
if not exist "%OUT%" mkdir "%OUT%" || exit /b 1
cl.exe /nologo /std:c++17 /EHsc /W4 /WX /O2 /MT /utf-8 /permissive- /DUNICODE /D_UNICODE "%SRC%" /Fo"%OUT%\\" /Fe"%OUT%\nulldc3-winrun.exe" /link /SUBSYSTEM:CONSOLE user32.lib advapi32.lib || exit /b 1
echo bootstrap.cmd: %OUT%\nulldc3-winrun.exe

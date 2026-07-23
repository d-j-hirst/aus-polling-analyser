@echo off
setlocal EnableExtensions EnableDelayedExpansion

where cl.exe >nul 2>nul
if errorlevel 1 (
	set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
	if not exist "!VSWHERE!" (
		echo Error: Visual Studio's C++ build tools were not found.
		exit /b 1
	)
	for /f "usebackq delims=" %%I in (`"!VSWHERE!" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
		set "VS_INSTALLATION=%%I"
	)
	if not defined VS_INSTALLATION (
		echo Error: Visual Studio's x64 C++ build tools were not found.
		exit /b 1
	)
	call "!VS_INSTALLATION!\VC\Auxiliary\Build\vcvars64.bat" >nul
	if errorlevel 1 exit /b 1
)

pushd "%~dp0"
if errorlevel 1 exit /b 1

set "SOURCES="
for /f "usebackq eol=# delims=" %%F in ("cli-sources.txt") do (
	set "SOURCES=!SOURCES! %%F"
)

set "OUTPUT_DIRECTORY=%~dp0cli-build"
set "OBJECT_DIRECTORY=%OUTPUT_DIRECTORY%\obj"
if not exist "%OBJECT_DIRECTORY%" mkdir "%OBJECT_DIRECTORY%"
if errorlevel 1 (
	popd
	exit /b 1
)

cl.exe /nologo /std:c++20 /O2 /EHsc /MT /MP /DNDEBUG /W3 ^
	/wd4244 /wd4267 /I. ^
	/Fo"%OBJECT_DIRECTORY%\\" ^
	/Fe"%OUTPUT_DIRECTORY%\polling-cli.exe" ^
	%SOURCES% PollingCli.cpp
set "BUILD_RESULT=%ERRORLEVEL%"

if "%BUILD_RESULT%"=="0" (
	echo Built %OUTPUT_DIRECTORY%\polling-cli.exe
)

popd
exit /b %BUILD_RESULT%

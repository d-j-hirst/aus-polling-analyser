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

set "TEST_DIRECTORY=%~dp0cli-build\tests"
set "OBJECT_DIRECTORY=%TEST_DIRECTORY%\obj"
if exist "%TEST_DIRECTORY%" rmdir /s /q "%TEST_DIRECTORY%"
mkdir "%OBJECT_DIRECTORY%"
if errorlevel 1 goto :failure

set "SOURCES="
set "OBJECTS="
for /f "usebackq eol=# delims=" %%F in ("cli-sources.txt") do (
	set "SOURCES=!SOURCES! %%F"
	set OBJECTS=!OBJECTS! "%OBJECT_DIRECTORY%\%%~nF.obj"
)

cl.exe /nologo /std:c++20 /O1 /EHsc /MT /MP /W3 ^
	/wd4244 /wd4267 /I. /c %SOURCES% /Fo"%OBJECT_DIRECTORY%\\"
if errorlevel 1 goto :failure

for /f "usebackq eol=# delims=" %%T in ("portable-test-sources.txt") do (
	cl.exe /nologo /std:c++20 /O1 /EHsc /MT /W3 ^
		/wd4244 /wd4267 /I. ^
		/Fo"%TEST_DIRECTORY%\%%T.obj" ^
		/Fe"%TEST_DIRECTORY%\%%T.exe" ^
		"tests\%%T.cpp" !OBJECTS!
	if errorlevel 1 goto :failure
)

set "FORECASTS="
for /d %%D in ("forecasts\*") do (
	if exist "%%~D\forecast.json" (
		set FORECASTS=!FORECASTS! "%%~D\forecast.json"
	)
)

call :run DateTests
if errorlevel 1 goto :failure
call :run ForecastSpecificationTests
if errorlevel 1 goto :failure
call :run LiveDataTests
if errorlevel 1 goto :failure
call :run MacroTargetResolverTests
if errorlevel 1 goto :failure
call :run RandomGeneratorTests
if errorlevel 1 goto :failure
call :run TerminalMacroFeedbackTests
if errorlevel 1 goto :failure
call :run WorkspacePathsTests "."
if errorlevel 1 goto :failure
call :run CoreReportSummaryTests
if errorlevel 1 goto :failure
echo Running ForecastSpecificationTests against committed forecasts
"%TEST_DIRECTORY%\ForecastSpecificationTests.exe" "." !FORECASTS!
if errorlevel 1 goto :failure
echo Running ForecastSpecificationProjectAdapterTests
"%TEST_DIRECTORY%\ForecastSpecificationProjectAdapterTests.exe" "." !FORECASTS!
if errorlevel 1 goto :failure
echo Running CliDependencyBoundaryTests
"%TEST_DIRECTORY%\CliDependencyBoundaryTests.exe" "." !FORECASTS!
if errorlevel 1 goto :failure

echo All portable tests passed.
popd
exit /b 0

:run
echo Running %~1
"%TEST_DIRECTORY%\%~1.exe" %2 %3 %4 %5 %6 %7 %8 %9
if errorlevel 1 exit /b 1
exit /b 0

:failure
set "RESULT=%ERRORLEVEL%"
if "%RESULT%"=="0" set "RESULT=1"
popd
exit /b %RESULT%

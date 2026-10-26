@echo off
rem Windows'ta MSVC 2022 + Qt 6 ile derle, test et ve dagitim klasoru olustur.
rem Kullanim:  scripts\windows-derle.bat [Qt yolu]
rem Varsayilan Qt yolu: C:\Qt\6.8.3\msvc2022_64
setlocal
set QTDIR=%~1
if "%QTDIR%"=="" set QTDIR=C:\Qt\6.8.3\msvc2022_64

set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
for /f "usebackq tokens=*" %%i in (`%VSWHERE% -latest -property installationPath`) do set VSDIR=%%i
call "%VSDIR%\VC\Auxiliary\Build\vcvars64.bat" >nul || exit /b 1

set PATH=%QTDIR%\bin;%PATH%
cd /d "%~dp0.."

cmake -S . -B build-win -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=%QTDIR% -DCIHAZ_WERROR=ON || exit /b 1
cmake --build build-win || exit /b 1
ctest --test-dir build-win --output-on-failure || exit /b 1

rem Dagitim: exe'ler + windeployqt ile Qt DLL'leri, QML modulleri ve eklentiler
set DIST=build-win\dagitim
if exist %DIST% rmdir /s /q %DIST%
mkdir %DIST%
rem qt_standard_project_setup Windows'ta tum exe'leri derleme kokune koyar
copy build-win\cihaz-izleme.exe %DIST% >nul
copy build-win\cihaz-cli.exe %DIST% >nul
copy build-win\kayit-goruntuleyici.exe %DIST% >nul
copy sim\msd200_sim.py %DIST% >nul
windeployqt --release --qmldir app\qml --no-translations %DIST%\cihaz-izleme.exe || exit /b 1
windeployqt --release --no-translations %DIST%\kayit-goruntuleyici.exe || exit /b 1
echo.
echo Dagitim klasoru: %DIST%

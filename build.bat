@echo off
REM Needs ONE of: MinGW-w64 g++ (e.g. w64devkit from github.com/skeeto/w64devkit) or Visual Studio Build Tools (run from "x64 Native Tools" prompt).
where g++ >nul 2>nul
if %errorlevel%==0 (
  windres app.rc -O coff -o app.res.o || goto :err
  g++ -std=c++17 -O2 -s -mwindows -static main.cpp app.res.o -o Pane.exe -lgdiplus -lgdi32 -lcomdlg32 -lole32 -lshell32 -luuid -ldwmapi -lshlwapi -luser32 -ladvapi32 || goto :err
  goto :ok
)
where cl >nul 2>nul
if %errorlevel%==0 (
  rc /nologo app.rc || goto :err
  cl /nologo /std:c++17 /EHsc /O2 /utf-8 main.cpp app.res /Fe:Pane.exe /link /SUBSYSTEM:WINDOWS gdiplus.lib gdi32.lib user32.lib comdlg32.lib ole32.lib shell32.lib dwmapi.lib shlwapi.lib advapi32.lib || goto :err
  goto :ok
)
echo No C++ compiler found. Install w64devkit or Visual Studio Build Tools. & pause & exit /b 1
:ok
echo. & echo Built Pane.exe & pause & exit /b 0
:err
echo Build failed - copy the error text and send it to me. & pause

@echo off
rem Builds X-Scope-Setup.exe with the C# compiler that ships with Windows (.NET Framework 4.x).
rem Needs ..\build\x-scope-1014d.bin (run make in FNIRSI-1014D first). Builds X Scope Remote from
rem "..\..\X Scope - Application" and embeds it together with the firmware and scope.bmp.
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
set APP=%~dp0..\..\X Scope - Application
call "%APP%\build.cmd" || exit /b 1
cd /d "%~dp0"
"%CSC%" /nologo /target:winexe /optimize+ /out:X-Scope-Setup.exe /win32manifest:app.manifest ^
  /win32icon:"%APP%\xscope.ico" ^
  /reference:System.Management.dll /reference:System.Windows.Forms.dll /reference:System.Drawing.dll ^
  /resource:..\build\x-scope-1014d.bin,x-scope-1014d.bin /resource:scope.bmp,scope.bmp ^
  /resource:"%APP%\X-Scope-Remote.exe",X-Scope-Remote.exe /resource:version.txt,version.txt ^
  XScopeInstaller.cs

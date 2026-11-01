@echo off
rem Builds X-Scope-Installer.exe with the C# compiler that ships with Windows (.NET Framework 4.x).
rem Needs ..\build\x-scope-1014d.bin (run make in FNIRSI-1014D first) and scope.bmp in this folder.
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
cd /d "%~dp0"
"%CSC%" /nologo /target:winexe /optimize+ /out:X-Scope-Installer.exe /win32manifest:app.manifest ^
  /reference:System.Management.dll /reference:System.Windows.Forms.dll /reference:System.Drawing.dll ^
  /resource:..\build\x-scope-1014d.bin,x-scope-1014d.bin /resource:scope.bmp,scope.bmp ^
  XScopeInstaller.cs

@echo off
rem Builds X-Scope-Remote.exe with the C# compiler that ships with Windows (.NET Framework 4.x).
setlocal
set CSC=%WINDIR%\Microsoft.NET\Framework64\v4.0.30319\csc.exe
if not exist "%CSC%" set CSC=%WINDIR%\Microsoft.NET\Framework\v4.0.30319\csc.exe
cd /d "%~dp0"
"%CSC%" /nologo /target:winexe /optimize+ /out:X-Scope-Remote.exe /win32icon:xscope.ico ^
  /reference:System.Windows.Forms.dll /reference:System.Drawing.dll /reference:System.Web.Extensions.dll ^
  XScopeRemote.cs Profiles.cs

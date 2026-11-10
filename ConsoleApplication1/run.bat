@echo off
setlocal
cd /d "%~dp0"
if not exist src\json.hpp (
 echo ERROR: keep your existing src\json.hpp next to main.cpp
 exit /b 1
)
where cl >nul 2>nul
if errorlevel 1 (
 echo Use Developer Command Prompt for Visual Studio to build.
 exit /b 1
)
if not exist build mkdir build
cl /nologo /EHsc /std:c++17 /utf-8 /Fo:build\ /Fe:build\DependencyVisualizer.exe src\main.cpp
if errorlevel 1 exit /b 1
build\DependencyVisualizer.exe %*

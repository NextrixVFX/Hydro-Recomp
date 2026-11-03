@echo off
setlocal
call D:\emsdk\emsdk_env.bat
cd /d "%~dp0.."
if not exist build\wasm mkdir build\wasm
emcc -c engine\entry.cxx -I engine -I hydro -std=c++23 -O1 -msimd128 -msse2 -o build\wasm\entry.o
if errorlevel 1 exit /b 1
emcc -c hydro\main.cxx -I hydro -I engine -std=c++23 -O1 -msimd128 -msse2 -o build\wasm\main.o
if errorlevel 1 exit /b 1
emcc -c engine\impl\inflate\puff.c -I engine -O1 -o build\wasm\puff.o
if errorlevel 1 exit /b 1
em++ build\wasm\entry.o build\wasm\main.o build\wasm\puff.o ^
  -O1 -msimd128 -msse2 ^
  -sUSE_WEBGL2=1 ^
  -sMIN_WEBGL_VERSION=2 ^
  -sMAX_WEBGL_VERSION=2 ^
  -sALLOW_MEMORY_GROWTH=1 ^
  -sINITIAL_MEMORY=268435456 ^
  -sASYNCIFY ^
  -sASYNCIFY_STACK_SIZE=65536 ^
  -sFORCE_FILESYSTEM=1 ^
  -sSTACK_SIZE=8388608 ^
  -o build\wasm\hydro.html
exit /b %ERRORLEVEL%

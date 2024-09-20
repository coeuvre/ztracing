@echo off
setlocal
cd /D "%~dp0"

:: --- Unpack Arguments -------------------------------------------------------
for %%a in (%*) do set "%%a=1"
if not "%--release%"=="1" set --debug=1
if "%--debug%"=="1"   set --release=0 && echo [debug mode]
if "%--release%"=="1" set --debug=0 && echo [release mode]

:: --- Unpack Command Line Build Arguments ------------------------------------
set auto_compile_flags=

:: --- Compile/Link Line Definitions ------------------------------------------
set cl_common=/I..\src\ /nologo /FC /Z7
set cl_debug=call cl /Od /DBUILD_DEBUG=1 %cl_common% %auto_compile_flags%
set cl_release=call cl /O2 /DBUILD_DEBUG=0 %cl_common% %auto_compile_flags%
set cl_link=/link /MANIFEST:EMBED /INCREMENTAL:NO /SUBSYSTEM:WINDOWS
set cl_out=/out:

:: --- Per-Build Settings -----------------------------------------------------

:: --- Choose Compile/Link Lines ----------------------------------------------
set compile_debug=%cl_debug%
set compile_release=%cl_release%
set compile_link=%cl_link%
set out=%cl_out%
if "%--debug%"=="1" set compile=%compile_debug%
if "%--release%"=="1" set compile=%compile_release%

:: --- Prep Directories -------------------------------------------------------
if not exist build mkdir build

:: --- Build Everything -------------------------------------------------------
pushd build
%compile% ..\src\ztracing_main.c %compile_link% %out%ztracing.exe || exit /b 1
popd

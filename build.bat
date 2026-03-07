@echo off
setlocal enabledelayedexpansion
cd /D "%~dp0"
:restart

for %%a in (%*) do set "%%~a=1"

if "%/help%"=="1" (
  call :show_help
  exit /b 0
)

set script_name=build.bat
set build_dir=build
set clang_ex=clang
set cl_ex=cl

if not "%/msvc%"=="1" if not "%/clang%"=="1" set "/msvc=1"
if not "%/release%"=="1" if not "%/relwithdebinfo%"=="1" set "/debug=1"
if "%/relwithdebinfo%"=="1" set "/debug=0"
if "%/relwithdebinfo%"=="1" set "/release=0"
if "%/debug%"=="1"          set "/release=0"
if "%/debug%"=="1"          set "/relwithdebinfo=0"
if "%/release%"=="1"        set "/debug=0"
if "%/release%"=="1"        set "/relwithdebinfo=0"
if "%/msvc%"=="1"           set "/clang=0"
if "%/clang%"=="1"          set "/msvc=0"
if "%/clang++%"=="1"        set "/msvc=0"
if "%/clang++%"=="1"        set "/clang=1"
if "%/clang++%"=="1"        set clang_ex=clang++ -x c++

set auto_compile_flags=
if "%/trace%"=="1"     set auto_compile_flags=%auto_compile_flags% -DTRACY_ENABLE -DTRACY_DELAYED_INIT src/third_party/tracy/public/TracyClient.cpp
if "%/vulkan%"=="1"    set auto_compile_flags=%auto_compile_flags% -DVULKAN_ENABLED /I"%VULKAN_SDK%\include"

set cl_common=/I "src" /nologo /FC /Fo"%build_dir%\\"
set cl_debug=call %cl_ex%   /MDd /Od /Ob1 /Z7 /DBUILD_DEBUG=1 %cl_common% %auto_compile_flags%
set cl_release=call %cl_ex% /MD  /O2          /DBUILD_DEBUG=0 %cl_common% %auto_compile_flags%
set cl_relwithdebinfo=call %cl_ex% /MD /O2 /Z7 /DBUILD_DEBUG=1 %cl_common% %auto_compile_flags%
set cl_link=/link /INCREMENTAL:NO /opt:ref /opt:icf /NOIMPLIB /NOEXP
if "%/vulkan%"=="1"    set cl_link=%cl_link% "%VULKAN_SDK%\lib\vulkan-1.lib"
set cl_shared=/LD
set cl_out=/out:
set clang_common=-Isrc -D_CRT_SECURE_NO_WARNINGS -Wno-writable-strings
if "%/vulkan%"=="1"    set clang_common=%clang_common% -DVULKAN_ENABLED -I"%VULKAN_SDK%/include"
set clang_debug=call %clang_ex% -g -O0 -DBUILD_DEBUG=1 -fno-omit-frame-pointer %clang_common% %auto_compile_flags%
set clang_release=call %clang_ex%  -O3 -DBUILD_DEBUG=0 %clang_common% %auto_compile_flags%
set clang_relwithdebinfo=call %clang_ex% -g -O2 -DBUILD_DEBUG=1 -fno-omit-frame-pointer %clang_common% %auto_compile_flags%
set clang_link=
if "%/vulkan%"=="1"    set clang_link=%clang_link% -L"%VULKAN_SDK%/lib" -lvulkan
set clang_shared=-shared
set clang_out=-o

if "%/msvc%"=="1"      set compile_debug=%cl_debug%
if "%/msvc%"=="1"      set compile_release=%cl_release%
if "%/msvc%"=="1"      set compile_relwithdebinfo=%cl_relwithdebinfo%
if "%/msvc%"=="1"      set compile_link=%cl_link%
if "%/msvc%"=="1"      set compile_shared=%cl_shared%
if "%/msvc%"=="1"      set out=%cl_out%
if "%/clang%"=="1"     set compile_debug=%clang_debug%
if "%/clang%"=="1"     set compile_release=%clang_release%
if "%/clang%"=="1"     set compile_relwithdebinfo=%clang_relwithdebinfo%
if "%/clang%"=="1"     set compile_link=%clang_link%
if "%/clang%"=="1"     set compile_shared=%clang_shared%
if "%/clang%"=="1"     set out=%clang_out%
if "%/debug%"=="1"     set compile=%compile_debug%
if "%/release%"=="1"   set compile=%compile_release%

call :print_info

if not exist %build_dir% mkdir %build_dir%

if "%/vulkan%"=="1" (
  if not exist %build_dir%\shaders mkdir %build_dir%\shaders
  glslc --target-env=vulkan1.3 -fshader-stage=rgen  src\raytracer\vulkan\shaders\rgen.glsl  -o %build_dir%\shaders\rgen.spv  || exit /b 1
  glslc --target-env=vulkan1.3 -fshader-stage=rchit src\raytracer\vulkan\shaders\rchit.glsl -o %build_dir%\shaders\rchit.spv || exit /b 1
  glslc --target-env=vulkan1.3 -fshader-stage=rmiss src\raytracer\vulkan\shaders\rmiss.glsl -o %build_dir%\shaders\rmiss.spv || exit /b 1
  echo - Shaders compiled to %build_dir%\shaders\
)

if "%all%"=="1"                 set didbuild=1 && call :build_all_demos || exit /b 1
if "%spheres%"=="1"             set didbuild=1 && call :build_demo spheres || exit /b 1
if "%tri%"=="1"                 set didbuild=1 && call :build_demo tri || exit /b 1
if "%cornell%"=="1"             set didbuild=1 && call :build_demo cornell || exit /b 1
if "%bunny%"=="1"               set didbuild=1 && call :build_demo bunny || exit /b 1

if "%didbuild%"=="" (
  echo Error: No valid target specified
  echo Try '%script_name% /help' for more information.
  exit /b 1
)

goto :eof

:: Subroutines
:build_all_demos
  call :build_demo spheres
  call :build_demo tri
  call :build_demo cornell
  call :build_demo bunny
exit /b 0

:build_demo
setlocal
set "demo_name=%~1"
shift

set "main_file=src\demos\%demo_name%\main.c"

echo %compile% "%main_file%" %compile_link% %out%%build_dir%\%demo_name%.exe
%compile% "%main_file%" %compile_link% %out%%build_dir%\%demo_name%.exe
endlocal
exit /b 0

:print_info
setlocal
echo - Build directory: %build_dir%
if "%msvc%"=="1" (
  echo - Compiler:        MSVC
) else if "%clang%"=="1" (
  echo - Compiler:        clang
)
if "%relwithdebinfo%"=="1" (
  echo - Mode:            RelWithDebInfo
) else if "%release%"=="1" (
  echo - Mode:            Release
) else (
  echo - Mode:            Debug
)
if "%trace%"=="1" (
  echo - Tracy:           Enabled
) else (
  echo - Tracy:           Disabled
)
if "%/vulkan%"=="1" (
  echo - Vulkan:          Enabled
)
endlocal
exit /b 0

:show_help
echo ========================================
echo         BUILD SCRIPT HELP
echo ========================================
echo.
echo USAGE:
echo   %script_name% [OPTIONS] [TARGETS]
echo.
echo OPTIONS:
echo   /msvc             Use MSVC compiler (default)
echo   /clang            Use Clang compiler
echo   /clang++          Use Clang++ compiler (C++)
echo   /debug            Debug build (default)
echo   /release          Release build
echo   /relwithdebinfo   RelWithDebInfo build
echo   /vulkan           Enable Vulkan support
echo   /trace            Enable Tracy profiling
echo   /help             Show this help message
echo.
echo TARGETS:
echo   all               Build all demos
echo   spheres           Build spheres demo
echo   tri               Build tri demo
echo   cornell           Build Cornell box demo
echo.
exit /b 0
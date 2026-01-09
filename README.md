## Platforms
- Windows x86/x64
- Linux x86/x64/ARM32/ARM64

No special CPU instructions are required.

## Compiling
The build is managed through the `build.bat` script on Windows and the `build.sh` script on Linux, refer to the respective help messages for usage instructions. 
- Cross-compiles as C and C++
- MSVC/gcc/g++/clang/clang++ recent enough to support C11
- Only depends on libc, libm and a few core OS APIs

### Windows
Building on Windows requires either MSVC or clang to be installed and the command line to be correctly configured. 
This involves installing the C++ build tools for Visual Studio and running `build.bat` from the Native Tools command prompt. 
See [this article](https://learn.microsoft.com/en-us/cpp/build/building-on-the-command-line?view=msvc-170) for more information on how to setup the Visual Studio command line tools.

## Profiling
Support for profiling with [Tracy](https://github.com/wolfpld/tracy) can be included by adding `--trace`. A submodule is
included in the repo under `src/third_party/tracy` which needs to be initialised and is 
where you can build the profiling tools. Profiling requires a C++ compiler, since MSVC does not support
compound literals in C++, profiling on Windows requires compiling with clang++.
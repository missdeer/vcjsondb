# vcjsondb

Export compiler commands database from Microsoft Visual Studio .sln/vcxproj file.

## Build

### Install Boost for MSVC

```bash
vcpkg.exe install boost-program-options[core]:x64-windows boost-program-options[core]:x86-windows
vcpkg.exe install boost-filesystem[core]:x64-windows boost-filesystem[core]:x86-windows
```

### Build with CMake

```bash
cmake.exe -S . -B build
cmake.exe --build build
```

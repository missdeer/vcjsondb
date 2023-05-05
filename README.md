# vcjsondb

Export compiler commands database from Microsoft Visual Studio .sln/vcxproj file.

## Build

### Install Boost for MSVC

```bash
C:
cd \
git clone https://github.com/microsoft/vcpkg.git
cd vcpkg
bootstrap-vcpkg.bat

vcpkg integrate install

vcpkg install boost-program-options[core]:x64-windows boost-program-options[core]:x86-windows
vcpkg install boost-filesystem[core]:x64-windows boost-filesystem[core]:x86-windows
```

### Build with CMake

```bash
cmake.exe -G Ninja -DCMAKE_BUILD_TYPE=RelWithDeb -DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows -S . -B build
cmake.exe --build build
```

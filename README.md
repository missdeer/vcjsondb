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

vcpkg install boost-program-options[core]:x64-windows-static
vcpkg install boost-filesystem[core]:x64-windows-static
```

### Build with CMake

```bash
cmake.exe -G Ninja -DCMAKE_BUILD_TYPE=RelWithDeb -DCMAKE_PREFIX_PATH=C:/vcpkg/installed/x64-windows -DVCPKG_TARGET_TRIPLET=x64-windows-static -S . -B build
cmake.exe --build build
```

## Usage

```
vcjsondb.exe -i "C:\path\to\your\project\project1.sln" -i "C:\path\to\your\project\project2.sln" -o "C:\path\to\output\directory"
```
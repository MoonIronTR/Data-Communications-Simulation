# Data-Communications-Simulation

A simple Stop‑&‑Wait simulator with CRC & checksum, built as a single‑file Qt Widgets application.

## 📋 Prerequisites

- **CMake ≥ 3.22**  
- **Qt 6** (e.g. Qt 6.9.0 for MSVC 2022)  
- **Visual Studio 2022** (with “Desktop development with C++” workload)  
- **windeployqt** (bundled with your Qt installation)
  
## 🔧 Configuration

1. **Point to your Qt installation**  
   In `CMakeLists.txt`, update the `QT_DIR` variable to match your Qt path:
   ```cmake
   set(QT_DIR "C:/Qt/6.9.0/msvc2022_64")
   
Ensure CMake can find Qt
The CMAKE_PREFIX_PATH is set from QT_DIR, so no further edits are usually needed.

# 1) Go to project root
cd "C:\path\to\your\repo"

# 2) Remove any previous build folder
Remove-Item -Recurse -Force build -ErrorAction Ignore

# 3) Generate VS2022 solution
cmake -S . -B build -G "Visual Studio 17 2022"

# 4) Build the Release configuration
cmake --build build --config Release


📦 Bundle Qt DLLs
After a successful build, you need to deploy Qt’s DLLs so the EXE runs on machines without Qt:

Open cmd.exe (not PowerShell).

Run:
cd build\Release
"C:\Qt\6.9.0\msvc2022_64\bin\windeployqt.exe" DatalinkSimQt.exe -quiet
This will copy all required Qt runtime libraries (including platforms\qwindows.dll) into build\Release.

▶️ Run the Application
From the same folder:

cd build\Release
DatalinkSimQt.exe

1- Click Gözat… and select veri.dat.

2- Click Simülasyonu başlat to see CRC list, flow events, LED status.

3- When simulation finishes, click Mesajı Göster to open a new window with the decoded file contents.

cmake_minimum_required(VERSION 3.22)

# -------------------------------------------------
project(DatalinkSimQt LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if (MSVC)
    add_compile_options(/Zc:__cplusplus)
endif()

# ------- Qt kurulum yolu (kendi yolunuza göre değiştirin) ----------
set(QT_DIR "C:/Qt/6.9.0/msvc2022_64")
set(CMAKE_PREFIX_PATH "${QT_DIR}")

# Q_OBJECT tespiti
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets)

# -------------------------------------------------
add_executable(DatalinkSimQt
    viba-20242-proje-30.cpp          # tek kaynak dosya
)

target_link_libraries(DatalinkSimQt
    PRIVATE Qt6::Widgets
)

# -------------------------------------------------
#  DLL’leri otomatik kopyala (windeployqt)
#  * -quiet  → tek tire!
# -------------------------------------------------
add_custom_command(TARGET DatalinkSimQt POST_BUILD
    COMMAND "${QT_DIR}/bin/windeployqt.exe"
            "$<TARGET_FILE:DatalinkSimQt>"
            -quiet                                # <-- düzeltildi
    COMMENT "windeployqt: Qt DLL'leri ve kaynakları kopyalanıyor..."
)

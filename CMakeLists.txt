cmake_minimum_required(VERSION 3.15)
project(MySaaSApp)

# C++17 Standardını zorunlu kılıyoruz
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# =========================================================================
# 1. KÜTÜPHANELERİ BUL (VCPKG ÜZERİNDEN)
# =========================================================================
find_package(Crow CONFIG REQUIRED)
find_package(sqlite3 CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)
find_package(argon2 CONFIG REQUIRED) # Şifreleme (Hash) Kütüphanesi Eklendi

# =========================================================================
# 2. PROJE DOSYALARINI OTOMATİK TOPLA
# =========================================================================
file(GLOB_RECURSE API_SOURCES "src/*.cpp")

# AdminUI.cpp dosyasını API kaynaklarından çıkar (Sadece Windows'ta derlenecek)
list(REMOVE_ITEM API_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/src/AdminUI.cpp")

# =========================================================================
# 3. LINUX VE WINDOWS İÇİN ORTAK BACKEND API SUNUCUSU
# =========================================================================
add_executable(MySaaSApp ${API_SOURCES})

# Bulunan kütüphaneleri API motorumuza statik olarak bağlıyoruz
target_link_libraries(MySaaSApp PRIVATE 
    Crow::Crow 
    sqlite3 
    nlohmann_json::nlohmann_json 
    cpr::cpr 
    jwt-cpp::jwt-cpp
    argon2
)

# Linux sistemlerde C++ thread yapısı için arka planda pthread zorunludur
if(UNIX AND NOT APPLE)
    target_link_libraries(MySaaSApp PRIVATE pthread)
endif()

# =========================================================================
# 4. SADECE WINDOWS İÇİN SÜPER ADMIN PANELİ (Arayüz)
# =========================================================================
if(WIN32)
    # Sadece Windows işletim sisteminde AdminUI.exe dosyasını derler
    add_executable(AdminUI src/AdminUI.cpp)
    
    # Not: Eğer projenizde ImGui/GLFW kütüphaneleri vcpkg ile kuruluysa
    # alt satıra onları da eklemelisiniz (Örn: imgui::imgui glfw)
    target_link_libraries(AdminUI PRIVATE 
        cpr::cpr 
        nlohmann_json::nlohmann_json
    )
endif()
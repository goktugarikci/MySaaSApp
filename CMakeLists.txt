cmake_minimum_required(VERSION 3.15)
project(MySaaSApp VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ==========================================================
# 1. GEREKLİ KÜTÜPHANELERİ BUL (vcpkg üzerinden)
# ==========================================================
find_package(Crow CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED)
find_package(imgui CONFIG REQUIRED)
find_package(glfw3 CONFIG REQUIRED)
find_package(OpenGL REQUIRED)
find_package(unofficial-argon2 CONFIG REQUIRED)
find_package(jwt-cpp CONFIG REQUIRED)

# [YENİ] nlohmann_json (JSON parse kütüphanesi) eklendi
find_package(nlohmann_json CONFIG REQUIRED)

# ==========================================================
# 2. PROGRAM: BACKEND SUNUCUSU (MySaaSApp.exe)
# ==========================================================
# Yeni modüler mimarideki tüm route (rota) dosyalarını dahil ediyoruz
add_executable(MySaaSApp 
    src/main.cpp 
    src/db/DatabaseManager.cpp 
    src/utils/Security.cpp 
    src/utils/FileManager.cpp

    src/routes/AuthRoutes.cpp
    src/routes/UserRoutes.cpp
    src/routes/ServerRoutes.cpp
    src/routes/MessageRoutes.cpp
    src/routes/KanbanRoutes.cpp
    src/routes/WsRoutes.cpp
    src/routes/AdminRoutes.cpp
)

target_link_libraries(MySaaSApp PRIVATE 
    Crow::Crow 
    unofficial::sqlite3::sqlite3
    cpr::cpr
    unofficial::argon2::libargon2
    jwt-cpp::jwt-cpp
)

# ==========================================================
# 3. PROGRAM: SÜPER ADMIN ARAYÜZÜ (AdminUI.exe)
# ==========================================================
add_executable(AdminUI 
    src/AdminUI.cpp
)

# AdminUI sadece REST API üzerinden Backend ile haberleşeceği için
# doğrudan sqlite3 veya crow'a ihtiyacı yoktur, CPR ve ImGui yeterlidir.
target_link_libraries(AdminUI PRIVATE 
    imgui::imgui
    glfw
    OpenGL::GL
    cpr::cpr
    nlohmann_json::nlohmann_json
)
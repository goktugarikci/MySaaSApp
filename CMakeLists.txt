cmake_minimum_required(VERSION 3.15)

project(MySaaSApp)

# 1. Paketleri Bul (vcpkg entegrasyonu)
find_package(fmt CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)
find_package(LibDataChannel CONFIG REQUIRED)
find_package(unofficial-sqlite3 CONFIG REQUIRED)
find_package(unofficial-argon2 CONFIG REQUIRED)
find_package(cpr CONFIG REQUIRED) # YENİ: Google Auth için

# 2. Kaynak Dosyaları Tanımla
# Dosyaların "src/" klasörü altında olduğunu belirtiyoruz.
add_executable(MySaaSApp
    src/main.cpp
    src/main.h
    # Eğer bu dosyalar henüz yoksa aşağıdaki satırın başına # koyarak yorum satırı yap!
    src/db/DatabaseManager.cpp 
    src/db/DatabaseManager.h
    src/utils/Security.cpp
    src/utils/Security.h "src/utils/FileManager.h" "src/utils/FileManager.cpp" "src/models/User.h" "src/models/Server.h" "src/models/Message.h" "src/models/Kanban.h" "src/models/Requests.h")

# 3. Kütüphaneleri Bağla (Link)
target_link_libraries(MySaaSApp PRIVATE
    fmt::fmt
    nlohmann_json::nlohmann_json
    Crow::Crow
    unofficial::sqlite3::sqlite3
    unofficial::argon2::libargon2
    cpr::cpr # YENİ
)

# C++ Standardını Belirle (C++17 veya 20)
target_compile_features(MySaaSApp PRIVATE cxx_std_17)
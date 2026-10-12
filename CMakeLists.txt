# Корневая сборка Aura: сервер (C++17) и/или desktop-клиент (Qt 6).
#   cmake -S . -B build                          — сервер + тесты (по умолчанию)
#   cmake -S . -B build -DAURA_WITH_CLIENT=ON     — плюс клиент
#   cmake -S . -B build -DAURA_WITH_SERVER=OFF -DAURA_WITH_CLIENT=ON — только клиент (Windows)
# Сборки попадают в build/server и build/client (так их ищут CI и docs).
cmake_minimum_required(VERSION 3.21)

project(aura VERSION 0.1.0 LANGUAGES C CXX)

option(AURA_WITH_SERVER "Собирать сервер (POSIX: Linux, macOS)" ON)
option(AURA_WITH_CLIENT "Собирать desktop-клиент (нужен Qt 6.5+)" OFF)

if(AURA_WITH_SERVER)
    enable_testing()
    add_subdirectory(server)
endif()

if(AURA_WITH_CLIENT)
    add_subdirectory(client)
endif()

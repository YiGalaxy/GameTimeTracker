cmake_minimum_required(VERSION 3.16)
project(GameTimeTracker VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ---- Qt 配置 ----
# 支持 Qt6 和 Qt5，自动检测
find_package(Qt6 COMPONENTS Widgets Sql Network QUIET)
if(NOT Qt6_FOUND)
    find_package(Qt5 5.15 REQUIRED COMPONENTS Widgets Sql Network)
    set(QT_VERSION_MAJOR 5)
    message(STATUS "Using Qt5")
else()
    set(QT_VERSION_MAJOR 6)
    message(STATUS "Using Qt6")
endif()

# ---- 源文件 ----
set(SOURCES
    src/main.cpp
    src/mainwindow.cpp
    src/gamemonitor.cpp
    src/datamanager.cpp
    src/authmanager.cpp
    src/syncmanager.cpp
    src/logindialog.cpp
    src/statswidget.cpp
    src/gamelistwidget.cpp
)

set(HEADERS
    src/mainwindow.h
    src/gamemonitor.h
    src/datamanager.h
    src/authmanager.h
    src/syncmanager.h
    src/logindialog.h
    src/statswidget.h
    src/gamelistwidget.h
)

set(RESOURCES
    resources/resources.qrc
)

# ---- 可执行文件 ----
add_executable(${PROJECT_NAME}
    ${SOURCES}
    ${HEADERS}
    ${RESOURCES}
)

# ---- 链接库 ----
if(QT_VERSION_MAJOR EQUAL 6)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Sql
        Qt6::Network
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt5::Widgets
        Qt5::Sql
        Qt5::Network
    )
endif()

# Windows 下需要链接 psapi（EnumProcesses 备用）和 ws2_32（网络）
if(WIN32)
    target_link_libraries(${PROJECT_NAME} PRIVATE psapi ws2_32)
endif()

# ---- 安装 ----
install(TARGETS ${PROJECT_NAME} DESTINATION bin)

# ---- 编译选项 ----
target_compile_options(${PROJECT_NAME} PRIVATE
    $<$<CXX_COMPILER_ID:MSVC>:/W4 /utf-8>
    $<$<CXX_COMPILER_ID:GNU>:-Wall -Wextra>
)

message(STATUS "GameTimeTracker configured successfully")
message(STATUS "  Build: cmake --build .")
message(STATUS "  Run:   ./GameTimeTracker")

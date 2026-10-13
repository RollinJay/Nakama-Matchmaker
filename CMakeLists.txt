cmake_minimum_required(VERSION 3.16)
project(NakamaMatchmaker VERSION 0.1.0 LANGUAGES CXX)

option(BUILD_LAUNCHER "Build the launcher app (Qt Widgets)" ON)
option(BUILD_SERVER "Build nakama-matchmaker-server, the community servers without a window (Qt Core and Network)" ON)
option(BUILD_TOOLS "Build the test tools" ON)

# Qt 6 when available, otherwise Qt 5 (5.10 or later); Widgets only for the launcher
set(QT_COMPONENTS Core Network)
if(BUILD_LAUNCHER)
	list(APPEND QT_COMPONENTS Widgets)
endif()
find_package(Qt6 COMPONENTS ${QT_COMPONENTS} QUIET)
if(Qt6Network_FOUND AND (NOT BUILD_LAUNCHER OR Qt6Widgets_FOUND))
	set(NETPLAY_ONLINE_QT Qt6)
else()
	find_package(Qt5 5.10 COMPONENTS ${QT_COMPONENTS} REQUIRED)
	set(NETPLAY_ONLINE_QT Qt5)
endif()

add_subdirectory(netplay-online)

if(BUILD_TOOLS)
	add_executable(netplay-online-test tools/online-test/main.cpp)
	set_target_properties(netplay-online-test PROPERTIES CXX_STANDARD 14 CXX_STANDARD_REQUIRED ON)
	target_link_libraries(netplay-online-test netplay-online)
endif()

if(BUILD_LAUNCHER OR BUILD_SERVER)
	add_subdirectory(launcher)
endif()

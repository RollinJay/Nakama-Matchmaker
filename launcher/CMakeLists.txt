# The community servers (docs/hosting.md): Qt Core and Network only, shared by the launcher
# and nakama-matchmaker-server
add_library(nakama-matchmaker-servers STATIC
	RankedSets.cpp
	RankedSets.h
	ServerCli.cpp
	ServerCli.h
	ServerConfig.cpp
	ServerConfig.h
	ServerHost.cpp
	ServerHost.h
	ServerManager.cpp
	ServerManager.h)
set_target_properties(nakama-matchmaker-servers PROPERTIES
	AUTOMOC ON
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)
target_include_directories(nakama-matchmaker-servers PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(nakama-matchmaker-servers PUBLIC ${NETPLAY_ONLINE_QT}::Core ${NETPLAY_ONLINE_QT}::Network)
if(WIN32)
	target_link_libraries(nakama-matchmaker-servers PUBLIC shell32)   # CommandLineToArgvW
endif()

# The servers' Lua modules (server/modules/README.md) go next to each program in the build
# folder, and to share/ when installed, where the programs look for them
function(copy_server_modules target)
	add_custom_command(TARGET ${target} POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E copy_directory ${PROJECT_SOURCE_DIR}/server/modules $<TARGET_FILE_DIR:${target}>/server-modules)
endfunction()
install(DIRECTORY ${PROJECT_SOURCE_DIR}/server/modules/ DESTINATION share/nakama-matchmaker/server-modules)

if(BUILD_LAUNCHER)
	add_executable(nakama-matchmaker WIN32
		main.cpp
		GameCatalog.cpp
		GameCatalog.h
		GameDialog.cpp
		GameDialog.h
		LauncherCore.cpp
		LauncherCore.h
		LauncherWindow.cpp
		LauncherWindow.h
		LeaderboardDialog.cpp
		LeaderboardDialog.h
		RankedPanel.cpp
		RankedPanel.h
		ServersWindow.cpp
		ServersWindow.h
		Settings.cpp
		Settings.h
		SettingsDialog.cpp
		SettingsDialog.h)
	set_target_properties(nakama-matchmaker PROPERTIES
		AUTOMOC ON
		CXX_STANDARD 14
		CXX_STANDARD_REQUIRED ON)
	target_link_libraries(nakama-matchmaker nakama-matchmaker-servers netplay-online ${NETPLAY_ONLINE_QT}::Widgets)
	copy_server_modules(nakama-matchmaker)
	install(TARGETS nakama-matchmaker RUNTIME DESTINATION bin)
endif()

if(BUILD_SERVER)
	add_executable(nakama-matchmaker-server server_main.cpp)
	set_target_properties(nakama-matchmaker-server PROPERTIES
		CXX_STANDARD 14
		CXX_STANDARD_REQUIRED ON)
	target_link_libraries(nakama-matchmaker-server nakama-matchmaker-servers)
	copy_server_modules(nakama-matchmaker-server)
	install(TARGETS nakama-matchmaker-server RUNTIME DESTINATION bin)
endif()

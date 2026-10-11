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
	Settings.cpp
	Settings.h
	SettingsDialog.cpp
	SettingsDialog.h)
set_target_properties(nakama-matchmaker PROPERTIES
	AUTOMOC ON
	CXX_STANDARD 14
	CXX_STANDARD_REQUIRED ON)
target_link_libraries(nakama-matchmaker netplay-online ${NETPLAY_ONLINE_QT}::Widgets)
install(TARGETS nakama-matchmaker RUNTIME DESTINATION bin)

add_executable(qt-terminal main.cpp vtscreen.cpp terminalwidget.cpp shellsession.cpp)
target_link_libraries(qt-terminal PRIVATE purecqt Qt6::QPurecIntegrationPlugin)

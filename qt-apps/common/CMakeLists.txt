add_library(purecqt STATIC platform.cpp theme.cpp)
target_include_directories(purecqt PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(purecqt PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)

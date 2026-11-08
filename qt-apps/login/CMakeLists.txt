add_library(purecauth STATIC
    auth.c
    ${PUREC_ROOT}/src/programs/accounts/accounts.c
    ${PUREC_ROOT}/src/programs/accounts/accounts_core.c)
target_include_directories(purecauth PRIVATE
    ${PUREC_ROOT}/src/libc/include
    ${PUREC_ROOT}/libxcrypt/include
    ${PUREC_ROOT}/src/programs/accounts)
target_link_libraries(purecauth PUBLIC ${PUREC_BIN}/lib/libpurecrypt.a ${PUREC_BIN}/lib/libpurec.a)

add_executable(qt-login main.cpp loginwindow.cpp)
target_include_directories(qt-login PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(qt-login PRIVATE purecqt purecauth)

qt_internal_add_plugin(QPurecIntegrationPlugin
    OUTPUT_NAME qpurec
    PLUGIN_TYPE platforms
    DEFAULT_IF "purec" IN_LIST QT_QPA_PLATFORMS
    SOURCES
        main.cpp
        qpurecintegration.cpp qpurecintegration.h
        qpurecwindow.cpp qpurecwindow.h
        qpurecbackingstore.cpp qpurecbackingstore.h
    INCLUDE_DIRECTORIES
        ${PUREC_ROOT_DIR}/src/libgui/include
        ${PUREC_ROOT_DIR}/src/libc/include
    LIBRARIES
        Qt::Core
        Qt::CorePrivate
        Qt::Gui
        Qt::GuiPrivate
        ${PUREC_BIN_DIR}/lib/libpuregui.a
        ${PUREC_BIN_DIR}/lib/libpurec.a
)

qt_internal_extend_target(QPurecIntegrationPlugin CONDITION QT_FEATURE_freetype
    LIBRARIES
        WrapFreetype::WrapFreetype
)

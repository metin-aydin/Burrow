find_package(Qt6 6.5 REQUIRED COMPONENTS Widgets)
find_package(FreeRDP 3 QUIET)
find_package(FreeRDP-Client 3 QUIET)

qt_standard_project_setup()

qt_add_executable(gremote
    src/main.cpp
    src/MainWindow.cpp
    src/MainWindow.h
    src/net/NetCore.cpp
    src/net/NetCore.h
    src/rdp/RdpEngine.h
    src/rdp/FreeRdpEngine.cpp
    src/rdp/FreeRdpEngine.h
)

target_include_directories(gremote PRIVATE src)
target_compile_definitions(gremote PRIVATE GREMOTE_VERSION="${PROJECT_VERSION}")
target_link_libraries(gremote PRIVATE Qt6::Widgets netcore::netcore)

if(FreeRDP_FOUND AND FreeRDP-Client_FOUND)
    message(STATUS "FreeRDP found: ${FreeRDP_VERSION}")
    target_link_libraries(gremote PRIVATE freerdp freerdp-client)
    target_compile_definitions(gremote PRIVATE GREMOTE_HAVE_FREERDP=1)
else()
    message(WARNING "FreeRDP 3 not found - building without RDP engine")
endif()

set_target_properties(gremote PROPERTIES
    OUTPUT_NAME "gremote"
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "gremote"
    MACOSX_BUNDLE_GUI_IDENTIFIER "dev.gremote.app"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    WIN32_EXECUTABLE ON)

install(TARGETS gremote BUNDLE DESTINATION . RUNTIME DESTINATION bin)

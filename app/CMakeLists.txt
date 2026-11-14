find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Network Quick QuickControls2 Svg LinguistTools)
find_package(FreeRDP 3 QUIET)
find_package(FreeRDP-Client 3 QUIET)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(burrow
    src/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/ListModels.cpp
    src/app/ListModels.h
    src/app/PreviewProvider.cpp
    src/app/PreviewProvider.h
    src/app/Updater.cpp
    src/app/Updater.h
    src/net/NetCore.cpp
    src/net/NetCore.h
    src/rdp/RdpEngine.h
    src/rdp/FreeRdpEngine.cpp
    src/rdp/FreeRdpEngine.h
    src/vault/Model.cpp
    src/vault/Model.h
    src/vault/VaultStore.cpp
    src/vault/VaultStore.h
    src/platform/Biometric.h
)

file(GLOB BURROW_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml)
file(GLOB BURROW_ICONS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/icons/*.svg)
set_source_files_properties(qml/Theme.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
qt_add_qml_module(burrow
    URI Burrow
    VERSION 1.0
    QML_FILES ${BURROW_QML}
    RESOURCES ${BURROW_ICONS} resources/icons/256x256.png
    NO_RESOURCE_TARGET_PATH
)

# Translations: English is the source language; Turkish ships as a compiled catalog.
qt_add_translations(burrow
    TS_FILES i18n/burrow_tr.ts
    RESOURCE_PREFIX /i18n
    LUPDATE_OPTIONS -no-obsolete)

if(APPLE)
    enable_language(OBJCXX)
    target_sources(burrow PRIVATE src/platform/Biometric_mac.mm)
    set_source_files_properties(src/platform/Biometric_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(burrow PRIVATE "-framework Foundation" "-framework LocalAuthentication"
                                          "-framework Security")
else()
    target_sources(burrow PRIVATE src/platform/Biometric_stub.cpp)
endif()

target_include_directories(burrow PRIVATE src)
target_compile_definitions(burrow PRIVATE BURROW_VERSION="${PROJECT_VERSION}")
target_link_libraries(burrow PRIVATE Qt6::Gui Qt6::Network Qt6::Quick Qt6::QuickControls2 Qt6::Svg netcore::netcore)

if(FreeRDP_FOUND AND FreeRDP-Client_FOUND)
    message(STATUS "FreeRDP found: ${FreeRDP_VERSION}")
    target_link_libraries(burrow PRIVATE freerdp freerdp-client)
    target_compile_definitions(burrow PRIVATE BURROW_HAVE_FREERDP=1)
else()
    message(WARNING "FreeRDP 3 not found - building without RDP engine")
endif()

if(APPLE)
    set(BURROW_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/resources/Burrow.icns)
    target_sources(burrow PRIVATE ${BURROW_ICNS})
    set_source_files_properties(${BURROW_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
    target_sources(burrow PRIVATE resources/burrow.rc)
endif()

set_target_properties(burrow PROPERTIES
    MACOSX_BUNDLE_ICON_FILE Burrow.icns
    OUTPUT_NAME "Burrow"
    MACOSX_BUNDLE ON
    MACOSX_BUNDLE_BUNDLE_NAME "Burrow"
    MACOSX_BUNDLE_GUI_IDENTIFIER "io.github.metin-aydin.burrow"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
    WIN32_EXECUTABLE ON)

install(TARGETS burrow BUNDLE DESTINATION . RUNTIME DESTINATION bin)

# --- Tests -------------------------------------------------------------------
find_package(Qt6 6.5 QUIET COMPONENTS Test)
if(Qt6Test_FOUND)
    qt_add_executable(tst_vault tests/tst_vault.cpp
        src/vault/Model.cpp src/vault/VaultStore.cpp src/net/NetCore.cpp
        src/app/Updater.cpp src/app/Updater.h)
    set_target_properties(tst_vault PROPERTIES MACOSX_BUNDLE OFF)
    target_include_directories(tst_vault PRIVATE src)
    target_link_libraries(tst_vault PRIVATE Qt6::Core Qt6::Network Qt6::Test netcore::netcore)
    add_test(NAME tst_vault COMMAND tst_vault)
endif()

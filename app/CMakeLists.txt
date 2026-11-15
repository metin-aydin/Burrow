find_package(Qt6 6.5 REQUIRED COMPONENTS Gui Widgets Network Concurrent Quick QuickControls2 Svg Multimedia LinguistTools)
find_package(FreeRDP 3 REQUIRED)
find_package(FreeRDP-Client 3 REQUIRED)
find_package(WinPR 3 REQUIRED)
# VNC (ADR 0008): Apple sign-in (DH + AES), VNC password (DES) and ZRLE (zlib).
if(APPLE AND NOT OPENSSL_ROOT_DIR AND EXISTS /opt/homebrew/opt/openssl@3)
    set(OPENSSL_ROOT_DIR /opt/homebrew/opt/openssl@3)
endif()
find_package(OpenSSL 3 REQUIRED COMPONENTS Crypto)
find_package(ZLIB REQUIRED)

qt_standard_project_setup(REQUIRES 6.8)

qt_add_executable(burrow
    src/main.cpp
    src/app/AppController.cpp
    src/app/AppController.h
    src/app/ListModels.cpp
    src/app/ListModels.h
    src/app/PreviewProvider.cpp
    src/app/PreviewProvider.h
    src/app/Tray.cpp
    src/app/Tray.h
    src/app/TunnelManager.cpp
    src/app/TunnelManager.h
    src/app/Updater.cpp
    src/app/Updater.h
    src/net/NetCore.cpp
    src/net/NetCore.h
    src/rdp/RdpEngine.h
    src/rdp/FreeRdpEngine.cpp
    src/rdp/FreeRdpEngine.h
    src/rdp/KeyMapper.cpp
    src/rdp/KeyMapper.h
    src/rdp/RdpClipboard.cpp
    src/rdp/RdpClipboard.h
    src/rdp/RdpSession.cpp
    src/rdp/RdpSession.h
    src/rdp/RemoteSession.h
    src/vnc/VncSession.cpp
    src/vnc/VncSession.h
    src/rdp/RdpView.cpp
    src/rdp/RdpView.h
    src/vault/Model.cpp
    src/vault/Model.h
    src/vault/VaultStore.cpp
    src/vault/VaultStore.h
    src/platform/Biometric.h
)

file(GLOB BURROW_QML RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} CONFIGURE_DEPENDS qml/*.qml qml/*.js)
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
    target_sources(burrow PRIVATE src/platform/Biometric_mac.mm src/platform/WindowChrome_mac.mm)
    set_source_files_properties(src/platform/Biometric_mac.mm src/platform/WindowChrome_mac.mm
                                PROPERTIES COMPILE_FLAGS "-fobjc-arc")
    target_link_libraries(burrow PRIVATE "-framework Foundation" "-framework LocalAuthentication"
                                          "-framework Security" "-framework AppKit")
else()
    target_sources(burrow PRIVATE src/platform/Biometric_stub.cpp src/platform/WindowChrome_stub.cpp)
endif()

target_include_directories(burrow PRIVATE src)
target_compile_definitions(burrow PRIVATE BURROW_VERSION="${PROJECT_VERSION}"
    # --seed-demo previews, read from the source tree (development only, not bundled).
    BURROW_DEMO_ASSETS="${CMAKE_SOURCE_DIR}/docs/assets/demo")
target_link_libraries(burrow PRIVATE Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Quick Qt6::QuickControls2 Qt6::Svg Qt6::Multimedia netcore::netcore)

message(STATUS "FreeRDP found: ${FreeRDP_VERSION}")
target_link_libraries(burrow PRIVATE freerdp freerdp-client winpr OpenSSL::Crypto ZLIB::ZLIB)
target_compile_definitions(burrow PRIVATE BURROW_HAVE_FREERDP=1)

if(APPLE)
    set(BURROW_ICNS ${CMAKE_CURRENT_SOURCE_DIR}/resources/Burrow.icns)
    target_sources(burrow PRIVATE ${BURROW_ICNS})
    set_source_files_properties(${BURROW_ICNS} PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
elseif(WIN32)
    target_sources(burrow PRIVATE resources/burrow.rc)
endif()

set_target_properties(burrow PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_CURRENT_SOURCE_DIR}/resources/Info.plist.in
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
        src/app/Updater.cpp src/app/Updater.h src/rdp/KeyMapper.cpp src/rdp/KeyMapper.h
        src/rdp/RdpClipboard.cpp src/rdp/RdpClipboard.h
        src/app/AppController.cpp src/app/AppController.h src/app/ListModels.cpp src/app/ListModels.h
        src/app/PreviewProvider.cpp src/app/PreviewProvider.h src/app/TunnelManager.cpp src/app/TunnelManager.h
        src/rdp/RdpSession.cpp src/rdp/RdpSession.h src/rdp/RemoteSession.h src/vnc/VncSession.cpp src/vnc/VncSession.h src/rdp/FreeRdpEngine.cpp src/rdp/FreeRdpEngine.h src/rdp/RdpEngine.h
        $<IF:$<BOOL:${APPLE}>,src/platform/Biometric_mac.mm,src/platform/Biometric_stub.cpp>)
    target_compile_definitions(tst_vault PRIVATE BURROW_DEMO_ASSETS="${CMAKE_SOURCE_DIR}/docs/assets/demo")
    target_compile_definitions(tst_vault PRIVATE BURROW_TEST_DATA="${CMAKE_CURRENT_SOURCE_DIR}/tests"
                                                 BURROW_VERSION="${PROJECT_VERSION}")
    set_target_properties(tst_vault PROPERTIES MACOSX_BUNDLE OFF)
    target_include_directories(tst_vault PRIVATE src)
    target_link_libraries(tst_vault PRIVATE Qt6::Gui Qt6::Network Qt6::Concurrent Qt6::Quick Qt6::Test netcore::netcore freerdp freerdp-client winpr OpenSSL::Crypto ZLIB::ZLIB)
    if(APPLE)
        set_source_files_properties(src/platform/Biometric_mac.mm PROPERTIES COMPILE_FLAGS "-fobjc-arc")
        target_link_libraries(tst_vault PRIVATE "-framework Foundation" "-framework LocalAuthentication" "-framework Security")
    endif()
    target_compile_definitions(tst_vault PRIVATE BURROW_UPDATER_ALLOW_INSECURE=1)
    add_test(NAME tst_vault COMMAND tst_vault)
    set_tests_properties(tst_vault PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

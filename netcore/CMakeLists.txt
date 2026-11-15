# Builds the Go netcore as a static C archive and exposes it as the
# imported target `netcore::netcore`.

find_program(GO_EXECUTABLE go REQUIRED)

set(NETCORE_OUT_DIR ${CMAKE_CURRENT_BINARY_DIR})
set(NETCORE_LIB ${NETCORE_OUT_DIR}/libnetcore.a)
set(NETCORE_HDR ${NETCORE_OUT_DIR}/libnetcore.h)

file(GLOB_RECURSE NETCORE_SOURCES CONFIGURE_DEPENDS
    ${CMAKE_CURRENT_SOURCE_DIR}/*.go
    ${CMAKE_CURRENT_SOURCE_DIR}/go.mod)

set(NETCORE_ENV CGO_ENABLED=1)
if(APPLE)
    list(APPEND NETCORE_ENV MACOSX_DEPLOYMENT_TARGET=${CMAKE_OSX_DEPLOYMENT_TARGET})
endif()

add_custom_command(
    OUTPUT ${NETCORE_LIB} ${NETCORE_HDR}
    COMMAND ${CMAKE_COMMAND} -E env ${NETCORE_ENV}
            ${GO_EXECUTABLE} build -buildmode=c-archive -trimpath
            -o ${NETCORE_LIB} .
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    DEPENDS ${NETCORE_SOURCES}
    COMMENT "Building Go netcore (c-archive)"
    VERBATIM)

add_custom_target(netcore_build DEPENDS ${NETCORE_LIB} ${NETCORE_HDR})

add_library(netcore STATIC IMPORTED GLOBAL)
set_target_properties(netcore PROPERTIES IMPORTED_LOCATION ${NETCORE_LIB})
add_dependencies(netcore netcore_build)
target_include_directories(netcore INTERFACE ${NETCORE_OUT_DIR})
# GlobalProtect uses libopenconnect (LGPL, linked dynamically and bundled with the app).
find_package(PkgConfig REQUIRED)
pkg_check_modules(OPENCONNECT REQUIRED IMPORTED_TARGET openconnect)
target_link_libraries(netcore INTERFACE PkgConfig::OPENCONNECT)

if(APPLE)
    target_link_libraries(netcore INTERFACE
        "-framework CoreFoundation" "-framework Security" "-framework SystemConfiguration"
        "-framework IOKit" resolv)
elseif(WIN32)
    # The Go runtime and its standard library (net, crypto, os/user) link against these on Windows.
    target_link_libraries(netcore INTERFACE ws2_32 winmm ntdll bcrypt userenv iphlpapi dnsapi secur32
        crypt32 ole32 oleaut32 advapi32 shell32 setupapi wtsapi32 netapi32 mswsock)
elseif(UNIX)
    target_link_libraries(netcore INTERFACE pthread dl resolv)
endif()
add_library(netcore::netcore ALIAS netcore)

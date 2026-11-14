cmake_minimum_required(VERSION 3.24)

# Version is injected by CI (release.yml) via -DBURROW_VERSION=X.Y.Z.
set(BURROW_VERSION "0.0.0" CACHE STRING "Application version")

# Must be set before project().
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "13.0" CACHE STRING "Minimum macOS version")
endif()

project(Burrow VERSION ${BURROW_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()
add_subdirectory(netcore)
add_subdirectory(app)

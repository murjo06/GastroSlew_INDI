project(indi_gastro_slew C CXX)
cmake_minimum_required(VERSION 3.16)

include(GNUInstallDirs)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake_modules/")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/../cmake_modules/")

set(BIN_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/bin")
set(DATA_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/share/indi/")
set(INDI_DATA_DIR "${CMAKE_INSTALL_PREFIX}/share/indi")

find_package(INDI REQUIRED)
find_package(Nova REQUIRED)
find_package(ZLIB REQUIRED)
find_package(GSL REQUIRED)

set(INDI_GASTRO_SLEW_VERSION_MAJOR 0)
set(INDI_GASTRO_SLEW_VERSION_MINOR 1)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/config.h.cmake ${CMAKE_CURRENT_BINARY_DIR}/config.h)
configure_file(${CMAKE_CURRENT_SOURCE_DIR}/indi_gastro_slew.xml.cmake ${CMAKE_CURRENT_BINARY_DIR}/indi_gastro_slew.xml)

include_directories(${CMAKE_CURRENT_BINARY_DIR})
include_directories(${CMAKE_CURRENT_SOURCE_DIR})
include_directories(${INDI_INCLUDE_DIR})
include_directories(${NOVA_INCLUDE_DIR})
include_directories(${EV_INCLUDE_DIR})

include(CMakeCommon)

add_executable(indi_gastro_slew indi_gastro_slew.cpp)
target_link_libraries(indi_gastro_slew ${INDI_LIBRARIES} ${NOVA_LIBRARIES} ${GSL_LIBRARIES})

if(CMAKE_SYSTEM_NAME MATCHES "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm*")
    target_link_libraries(indi_gastro_slew rt)
endif(CMAKE_SYSTEM_NAME MATCHES "Linux" AND CMAKE_SYSTEM_PROCESSOR MATCHES "arm*")

install(TARGETS indi_gastro_slew RUNTIME DESTINATION bin)

install(FILES  ${CMAKE_CURRENT_BINARY_DIR}/indi_gastro_slew.xml DESTINATION ${INDI_DATA_DIR})
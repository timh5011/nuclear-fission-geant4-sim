cmake_minimum_required(VERSION 3.16)

project(nuclear_fission)

##

option(WITH_GEANT4_UIVIS "Build example with Geant4 UI and Vis drivers" ON)
if(WITH_GEANT4_UIVIS)
  find_package(Geant4 REQUIRED ui_all vis_all)
else()
  find_package(Geant4 REQUIRED)
endif()

##

include(${Geant4_USE_FILE})
include_directories(${PROJECT_SOURCE_DIR}/include)

##

file(GLOB sources ${PROJECT_SOURCE_DIR}/src/*.cc)
file(GLOB headers ${PROJECT_SOURCE_DIR}/include/*.hh)


##

add_executable(nuclear_fission nuclear_fission.cc ${sources} ${headers})
target_link_libraries(nuclear_fission ${Geant4_LIBRARIES})

##

file(GLOB MACRO_FILES ${PROJECT_SOURCE_DIR}/macros/*.mac)
foreach(_script ${MACRO_FILES})
  configure_file(
    ${_script}
    ${PROJECT_BINARY_DIR}
    COPYONLY
    )
endforeach()

##




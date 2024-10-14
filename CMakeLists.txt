cmake_minimum_required(VERSION 3.5)

project(vcjsondb VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(Boost_NO_WARN_NEW_VERSIONS ON)

find_package(Boost REQUIRED program_options filesystem)

if(Boost_FOUND)
     include_directories(${Boost_INCLUDE_DIRS})
     link_directories(${Boost_LIBRARY_DIRS})
endif(Boost_FOUND)

# First check the command line variable VCPKG_TARGET_TRIPLET
if(DEFINED VCPKG_TARGET_TRIPLET)
    set(TRIPLET ${VCPKG_TARGET_TRIPLET})
else()
    # If it's not defined, check the environment variable
    set(TRIPLET $ENV{VCPKG_TARGET_TRIPLET})
endif()

# Now check if TRIPLET ends with "-windows-static"
string(FIND "${TRIPLET}" "-windows-static" POSITION)
if(${POSITION} GREATER_EQUAL 0)
    if(NOT MSVC)
        set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
    else()
        add_compile_options("/MT$<$<CONFIG:Debug>:d>")
        if(POLICY CMP0091)
            cmake_policy(SET CMP0091 NEW)
        endif()
    endif()
endif()

add_compile_definitions(STRSAFE_NO_DEPRECATE _WIN32_WINNT=0x0601)

set(PROJECT_SOURCES
    main.cpp
    utils.cpp
    utils.h
    )

IF(WIN32)
    list(APPEND PROJECT_SOURCES ${PROJECT_NAME}.rc)
ENDIF(WIN32)

add_executable(${PROJECT_NAME} ${PROJECT_SOURCES})

if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options Boost::filesystem)


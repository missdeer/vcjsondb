cmake_minimum_required(VERSION 3.5)

project(vcjsondb VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/rapidxml-1.13)

set(Boost_NO_WARN_NEW_VERSIONS ON)

find_package(Boost 1.79.0 REQUIRED program_options filesystem)

if(Boost_FOUND)
     include_directories(${Boost_INCLUDE_DIRS})
     link_directories(${Boost_LIBRARY_DIRS})

     set(Boost_USE_STATIC_LIBS ON)

     set(Boost_USE_MULTITHREADED ON)
     if(MSVC)
       set(Boost_USE_STATIC_RUNTIME ON)
     endif(MSVC)
endif(Boost_FOUND)

if(NOT MSVC)
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -static")
else()
    add_compile_options("/MT$<$<CONFIG:Debug>:d>")
    if(POLICY CMP0091)
        cmake_policy(SET CMP0091 NEW)
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

if(msvc)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

IF(Boost_FOUND)
    target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options Boost::filesystem)
    IF(NOT MSVC)
        target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
    ENDIF()
ENDIF(Boost_FOUND)

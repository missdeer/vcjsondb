cmake_minimum_required(VERSION 3.5)

project(vcjsondb VERSION 0.1 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/rapidxml-1.13)

find_package(Boost 1.79.0 REQUIRED)

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

IF(Boost_FOUND)
    IF(MSVC)
        target_link_directories(${PROJECT_NAME} PRIVATE ${Boost_INCLUDE_DIRS}/lib64-msvc-14.3)
    ELSE(MSVC)
        target_link_libraries(${PROJECT_NAME} boost_program_options-mt boost_filesystem-mt ws2_32)
    ENDIF(MSVC)
ENDIF(Boost_FOUND)

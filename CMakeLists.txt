cmake_minimum_required(VERSION 3.14)

project(mingwdeployqt LANGUAGES CXX)

set(CMAKE_INCLUDE_CURRENT_DIR ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(Boost_NO_WARN_NEW_VERSIONS ON)

find_package(Boost REQUIRED program_options)

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

add_executable(mingwdeployqt
  main.cpp
  mingwdeployqt.rc
)

if(MSVC)
    set_property(TARGET ${PROJECT_NAME} PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE ws2_32)
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE Boost::program_options)

if ($ENV{MSYSTEM} STREQUAL "")		
	set(INSTALL_DIR ${CMAKE_INSTALL_BINDIR})	
else()        			  
	set(INSTALL_DIR "/$ENV{MSYSTEM}/bin")
endif()

install(TARGETS ${PROJECT_NAME}
  RUNTIME DESTINATION ${INSTALL_DIR})

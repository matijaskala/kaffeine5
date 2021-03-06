project(kaffeine)
cmake_minimum_required(VERSION 2.8.12)

find_package(ECM REQUIRED NO_MODULE)
set(CMAKE_MODULE_PATH "${CMAKE_SOURCE_DIR}/cmake/modules" ${ECM_MODULE_PATH})

find_package(Qt5 REQUIRED COMPONENTS Network Sql Widgets X11Extras)
find_package(KF5 REQUIRED COMPONENTS Completion DBusAddons I18n IconThemes KIO Notifications WidgetsAddons XmlGui)
find_package(X11 REQUIRED)
find_package(VLC 1.2 REQUIRED)

include(CheckIncludeFiles)
include(KDEInstallDirs)
include(KDECompilerSettings)
include(ECMInstallIcons)
include(ECMOptionalAddSubdirectory)

set(CMAKE_AUTOMOC ON)

include_directories(${CMAKE_CURRENT_SOURCE_DIR}/include ${CMAKE_BINARY_DIR} ${VLC_INCLUDE_DIRS}
                    ${X11_Xscreensaver_INCLUDE_PATH})

if(STRICT_BUILD)
  add_definitions(-Werror -Weverything -Wno-packed -Wno-padded -Wno-sign-conversion -Wno-unreachable-code
                  -Wno-weak-vtables -ferror-limit=5
                  -isystem /usr/include/KDE
                  -isystem /usr/include/qt4/QtCore
                  -isystem /usr/include/qt4/QtGui
                  -isystem /usr/include/qt4/QtNetwork
                  -DQT_NO_CAST_FROM_ASCII -DQT_NO_CAST_FROM_BYTEARRAY -DQT_NO_CAST_TO_ASCII
                  -DQT_NO_URL_CAST_FROM_STRING -DQT_STRICT_ITERATORS)
endif(STRICT_BUILD)

include(CheckIncludeFiles)
check_include_files(${CMAKE_CURRENT_SOURCE_DIR}/include/frontend.h HAVE_DVB)

if(NOT HAVE_DVB)
  MESSAGE(STATUS "Disabled features:\nDVB  Digital television support for Linux\n")
  set(HAVE_DVB 0)
endif(NOT HAVE_DVB)

add_subdirectory(deviceactions)
add_subdirectory(dtvdaemon)
add_subdirectory(icons)
add_subdirectory(profiles)
add_subdirectory(src)
ecm_optional_add_subdirectory(po)

if(BUILD_TOOLS)
  add_subdirectory(tools)
endif(BUILD_TOOLS)

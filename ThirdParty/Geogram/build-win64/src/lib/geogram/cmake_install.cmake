# Install script for directory: C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/geogram-src/geogram_1.9.7/src/lib/geogram

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/Geogram")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(NOT CMAKE_INSTALL_LOCAL_ONLY)
  # Include the install script for the subdirectory.
  include("C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/src/lib/geogram/third_party/cmake_install.cmake")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "devkit" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/Debug/geogram.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/Release/geogram.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/MinSizeRel/geogram.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/RelWithDebInfo/geogram.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "devkit-full" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/Debug/geogram.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/Release/geogram.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/MinSizeRel/geogram.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/lib/RelWithDebInfo/geogram.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "devkit" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/geogram1/geogram" TYPE DIRECTORY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/geogram-src/geogram_1.9.7/src/lib/geogram/api" FILES_MATCHING REGEX "/[^/]*\\.h$")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "devkit-full" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/geogram1/geogram" TYPE DIRECTORY FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/geogram-src/geogram_1.9.7/src/lib/geogram/." FILES_MATCHING REGEX "/[^/]*\\.h$" REGEX "/license/" EXCLUDE)
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/pkgconfig" TYPE FILE FILES "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/geogram1.pc")
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "C:/Users/Michael/Documents/Unreal Projects/PlanetaryCreation/ThirdParty/Geogram/build-win64/src/lib/geogram/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()

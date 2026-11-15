cmake_minimum_required(VERSION 3.15)

set(PACKAGE GameplayFootball)

set(VERSION_MAJOR 0)
set(VERSION_MINOR 3)
set(VERSION_PATCH 0)
if(${VERSION_PATCH})
   set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR}.${VERSION_PATCH})
else(${VERSION_PATCH})
   set(VERSION ${VERSION_MAJOR}.${VERSION_MINOR})
endif(${VERSION_PATCH})

project(GameplayFootball)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(UNIX)
   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -g -Wall")
   # Temporarily suppress warnings during debug
   set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -Wno-unused-variable -Wno-sign-compare -Wno-unused-function -Wno-strict-aliasing")
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -Wall -O3")
endif(UNIX)

if(MSVC)
   set(CMAKE_CXX_FLAGS_RELEASE "${CMAKE_CXX_FLAGS_RELEASE} -O2 -GL")
   set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -LTCG")
   set(CMAKE_SHARED_LINKER_FLAGS "${CMAKE_SHARED_LINKER_FLAGS} -LTCG")
   set(CMAKE_STATIC_LINKER_FLAGS "${CMAKE_STATIC_LINKER_FLAGS} -LTCG")
endif(MSVC)

# Find required libraries
FIND_PACKAGE(OpenGL REQUIRED)
include_directories(${OPENGL_INCLUDE_DIR})

FIND_PACKAGE(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIR})

FIND_PACKAGE(SDL2_image CONFIG REQUIRED)
include_directories(${SDL2_IMAGE_DIRS})

FIND_PACKAGE(SDL2_ttf CONFIG REQUIRED)
include_directories(${SDL2_TTF_DIRS})

FIND_PACKAGE(sdl2-gfx CONFIG REQUIRED)
include_directories(${SDL2_GFX_DIRS})

FIND_PACKAGE(OpenAL CONFIG REQUIRED)
FIND_PACKAGE(unofficial-sqlite3 CONFIG REQUIRED)

if(DEFINED VCPKG_INSTALLED_DIR AND DEFINED VCPKG_TARGET_TRIPLET)
   include_directories("${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include"
                       "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/include/SDL2")
endif()

include_directories(${PROJECT_SOURCE_DIR}/src)

# Include the sources
include(sources.cmake)

# Compile it as multiple static libraries (note: not compiling physics,
# as not used by gameplayfootball)

add_library(baselib OBJECT ${BASE_SOURCES} ${BASE_HEADERS}
        ${BASE_GEOMETRY_HEADERS} ${BASE_MATH_HEADERS})
add_library(systemscommonlib OBJECT ${SYSTEMS_COMMON_SOURCES}
        ${SYSTEMS_COMMON_HEADERS})
add_library(systemsgraphicslib OBJECT ${SYSTEMS_GRAPHICS_SOURCES}
        ${SYSTEMS_GRAPHICS_HEADERS} ${SYSTEMS_GRAPHICS_OBJECTS_HEADERS}
        ${SYSTEMS_GRAPHICS_RESOURCES_HEADERS} ${SYSTEMS_GRAPHICS_RENDERING_HEADERS})
add_library(systemsaudiolib OBJECT ${SYSTEMS_AUDIO_SOURCES}
        ${SYSTEMS_AUDIO_HEADERS} ${SYSTEMS_AUDIO_OBJECTS_HEADERS}
        ${SYSTEMS_AUDIO_RESOURCES_HEADERS} ${SYSTEMS_AUDIO_RENDERING_HEADERS})
add_library(loaderslib OBJECT ${LOADERS_SOURCES} ${LOADERS_HEADERS})
add_library(typeslib OBJECT ${TYPES_SOURCES} ${TYPES_HEADERS})
add_library(frameworklib OBJECT ${FRAMEWORK_SOURCES} ${FRAMEWORK_HEADERS})
add_library(scenelib OBJECT ${SCENE_SOURCES} ${SCENE_HEADERS}
        ${SCENE2D_HEADERS} ${SCENE3D_HEADERS} ${SCENE_OBJECTS_HEADERS}
        ${SCENE_RESOURCES_HEADERS})
add_library(managerslib OBJECT ${MANAGERS_SOURCES} ${MANAGERS_HEADERS})
add_library(utilslib OBJECT ${UTILS_SOURCES} ${UTILS_HEADERS}
        ${UTILS_EXT_HEADERS})
add_library(gui2lib OBJECT ${UTILS_GUI2_SOURCES} ${UTILS_GUI2_HEADERS}
        ${UTILS_GUI2_WIDGETS_HEADERS})

set(OWN_LIBRARIES $<TARGET_OBJECTS:baselib> $<TARGET_OBJECTS:systemscommonlib>
        $<TARGET_OBJECTS:systemsgraphicslib>
        $<TARGET_OBJECTS:systemsaudiolib> $<TARGET_OBJECTS:loaderslib>
        $<TARGET_OBJECTS:typeslib> $<TARGET_OBJECTS:frameworklib>
        $<TARGET_OBJECTS:scenelib> $<TARGET_OBJECTS:managerslib>
        $<TARGET_OBJECTS:utilslib> $<TARGET_OBJECTS:gui2lib>)

# Join all created static libraries into the single static one.
add_library(blunted2 ${BLUNTED_CORE_SOURCES} ${BLUNTED_CORE_HEADERS} ${OWN_LIBRARIES})

add_library(leaguelib ${LEAGUE_SOURCES} ${LEAGUE_HEADERS})
add_library(gamelib ${GAME_SOURCES} ${GAME_HEADERS})
add_library(hidlib ${HID_SOURCES} ${HID_HEADERS})
add_library(menulib ${MENU_SOURCES} ${MENU_HEADERS})
add_library(datalib ${DATA_SOURCES} ${DATA_HEADERS})

set(LIBRARIES gamelib hidlib menulib datalib leaguelib blunted2
   SDL2::SDL2 SDL2::SDL2main SDL2_image::SDL2_image SDL2_ttf::SDL2_ttf SDL2::SDL2_gfx
   OpenAL::OpenAL unofficial::sqlite3::sqlite3 ${OPENGL_LIBRARY})
if(WIN32)
   list(APPEND LIBRARIES winmm)
endif()

add_executable(gameplayfootball WIN32 ${CORE_SOURCES} ${CORE_HEADERS})
target_link_libraries(gameplayfootball ${LIBRARIES})

add_custom_command(TARGET gameplayfootball POST_BUILD
   COMMAND ${CMAKE_COMMAND} -E copy_directory
   "${PROJECT_SOURCE_DIR}/data/media/shaders"
   "$<TARGET_FILE_DIR:gameplayfootball>/media/shaders"
   COMMENT "Syncing GLSL shaders to runtime media/shaders directory")

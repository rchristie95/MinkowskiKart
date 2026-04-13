# Modify this file to change the last-modified date when you add/remove a file.
# This will then trigger a new cmake run automatically.
# Stage 3 observer snapshot sources added.
file(GLOB_RECURSE STK_HEADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.hpp")
file(GLOB_RECURSE STK_SOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "src/*.cpp")
file(GLOB_RECURSE STK_SHADERS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "data/shaders/*")
file(GLOB_RECURSE STK_RESOURCES RELATIVE ${CMAKE_CURRENT_SOURCE_DIR} "${PROJECT_BINARY_DIR}/tmp/*.rc")

# --- Detect if the plugin is build out of tree or not ---
if(CMAKE_PROJECT_NAME STREQUAL "obs-studio")
  set(BUILD_OUT_OF_TREE OFF)
  if(OBS_CMAKE_VERSION VERSION_GREATER_EQUAL 3.0.0)
    legacy_check()
  endif()
else()
  set(BUILD_OUT_OF_TREE ON)
  cmake_minimum_required(VERSION 3.18)
endif()

project(recursion-effect VERSION 0.1.0)
set(PROJECT_FULL_NAME "Recursion Effect")

# Set new UUIDs when you start to create a new plugin.
set(MACOS_PACKAGE_UUID "7E7D46D5-372F-4E7B-ACBD-7D3052E13562")
set(MACOS_INSTALLER_UUID "9B213B47-C989-44C2-B648-508878C97CF7")

add_library(${PROJECT_NAME} MODULE)

configure_file(${CMAKE_CURRENT_SOURCE_DIR}/version.h.in ${CMAKE_CURRENT_SOURCE_DIR}/version.h)

target_sources(${PROJECT_NAME} PRIVATE
	recursion-effect.c
	recursion-effect.h
	version.h)

if(BUILD_OUT_OF_TREE)
	find_package(libobs REQUIRED)
	find_package(obs-frontend-api REQUIRED)
	include(cmake/ObsPluginHelpers.cmake)
endif()
  
if(OS_WINDOWS)
	get_filename_component(ISS_FILES_DIR "${CMAKE_BINARY_DIR}\\..\\package" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_FILES_DIR}" ISS_FILES_DIR)
	get_filename_component(ISS_PACKAGE_DIR "${CMAKE_PACKAGE_PREFIX}\\.." ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_PACKAGE_DIR}" ISS_PACKAGE_DIR)
	get_filename_component(ISS_SOURCE_DIR "${PROJECT_SOURCE_DIR}" ABSOLUTE)
	file(TO_NATIVE_PATH "${ISS_SOURCE_DIR}" ISS_SOURCE_DIR)
	configure_file("installer.iss.in"
			"${PROJECT_BINARY_DIR}/installer.iss"
		)
	configure_file(${CMAKE_CURRENT_SOURCE_DIR}/resource.rc.in ${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.rc)
	target_sources(${PROJECT_NAME} PRIVATE 
		${CMAKE_CURRENT_SOURCE_DIR}/${PROJECT_NAME}.rc)
elseif(OS_MACOS)
	set(MACOS_BUNDLEID "com.exeldro.${PROJECT_NAME}")
	set(MACOSX_PLUGIN_GUI_IDENTIFIER "${MACOS_BUNDLEID}")
	set(MACOSX_PLUGIN_BUNDLE_VERSION "${PROJECT_VERSION}")
	set(MACOSX_PLUGIN_SHORT_VERSION_STRING "1")
	configure_file(cmake/bundle/macos/installer-macos.pkgproj.in ${CMAKE_BINARY_DIR}/installer-macos.generated.pkgproj)
	target_compile_options(${PROJECT_NAME} PRIVATE -Wall)
endif()

target_link_libraries(${PROJECT_NAME}
	OBS::libobs)

if(BUILD_OUT_OF_TREE)
    if(NOT LIB_OUT_DIR)
        set(LIB_OUT_DIR "/lib/obs-plugins")
    endif()
    if(NOT DATA_OUT_DIR)
        set(DATA_OUT_DIR "/share/obs/obs-plugins/${PROJECT_NAME}")
    endif()
    set_target_properties(${PROJECT_NAME} PROPERTIES PREFIX "")
    install(TARGETS ${PROJECT_NAME}
        LIBRARY DESTINATION ${CMAKE_INSTALL_PREFIX}/${LIB_OUT_DIR})
    install(DIRECTORY data/locale
        DESTINATION ${CMAKE_INSTALL_PREFIX}/${DATA_OUT_DIR})
    setup_plugin_target(${PROJECT_NAME})
else()
    if(OBS_CMAKE_VERSION VERSION_GREATER_EQUAL 3.0.0)
        set_target_properties_obs(${PROJECT_NAME} PROPERTIES FOLDER "plugins/exeldro" PREFIX "")
    else()
        set_target_properties(${PROJECT_NAME} PROPERTIES FOLDER "plugins/exeldro")
        setup_plugin_target(${PROJECT_NAME})
    endif()
endif()

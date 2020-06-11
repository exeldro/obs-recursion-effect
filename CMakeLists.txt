project(recursion-effect)

set(recursion-effect_HEADERS
	recursion-effect.h)
set(recursion-effect_SOURCES
	recursion-effect.c)

if(WIN32)
	list(APPEND recursion-effect_SOURCES
		recursion-effect.rc)
endif()

add_library(recursion-effect MODULE
	${recursion-effect_HEADERS}
	${recursion-effect_SOURCES})
target_link_libraries(recursion-effect
	libobs)

set_target_properties(recursion-effect PROPERTIES FOLDER "plugins/exeldro")
install_obs_plugin_with_data(recursion-effect data)

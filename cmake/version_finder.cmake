find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
	message(STATUS "Found git")
	execute_process(
		COMMAND ${GIT_EXECUTABLE} tag --points-at HEAD
		OUTPUT_VARIABLE GIT_TAG
		OUTPUT_STRIP_TRAILING_WHITESPACE
	)
else()
	message(WARNING "Cannot find git")
	set(GIT_TAG "")
endif()

add_library(ccc_versioninfo STATIC
	${CMAKE_SOURCE_DIR}/cmake/git_tag.cpp
)
target_compile_definitions(ccc_versioninfo PRIVATE -DGIT_TAG="${GIT_TAG}")

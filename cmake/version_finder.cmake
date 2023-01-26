# Fetch the git tag and generate a .h/.cpp file pair that defines the git_tag
# function to make it available at runtime.
set(GIT_TAG_CPP_PATH "${CMAKE_BINARY_DIR}/git_tag.cpp")
set(GIT_TAG_CPP [[
const char* git_tag() {
	return ""
		#include "git_tag.h"
	;
}
]])
set(GIT_TAG_H_PATH "${CMAKE_BINARY_DIR}/git_tag.h")
set(GIT_TAG_FORMAT [[\"%\(refname:strip=2\)\"]])
file(WRITE "${GIT_TAG_CPP_PATH}" "${GIT_TAG_CPP}")
file(WRITE "${GIT_TAG_H_PATH}" "")
find_package(Git QUIET)
if(GIT_FOUND AND EXISTS "${CMAKE_SOURCE_DIR}/.git")
	message(STATUS "Found git")
	add_custom_command(
		COMMAND ${GIT_EXECUTABLE} tag --points-at HEAD --format "${GIT_TAG_FORMAT}" > "${GIT_TAG_H_PATH}"
		DEPENDS "${CMAKE_SOURCE_DIR}/*"
		OUTPUT "${GIT_TAG_H_PATH}"
	)
else()
	message(WARNING "Cannot find git")
endif()

add_library(versioninfo STATIC
	"${GIT_TAG_CPP_PATH}"
	"${GIT_TAG_H_PATH}"
)

# Package up all the files for a release. This is to be run as part of a CI job.
if(ZIP_RELEASE)
	if(GIT_TAG STREQUAL "")
		string(TIMESTAMP RELEASE_DATE "%Y-%m-%d_%H-%M-%S")
		string(SUBSTRING "${GIT_COMMIT}" 0 7 GIT_SHORT_COMMIT)
		set(RELEASE_VERSION "${RELEASE_DATE}-${GIT_SHORT_COMMIT}")
	else()
		set(RELEASE_VERSION ${GIT_TAG})
	endif()
	
	if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
		if(USE_MUSL_LIBC)
			set(RELEASE_OS "linux-musl")
		else()
			execute_process(
				COMMAND bash -c "ldd --version | sed -n 's/ldd (Ubuntu GLIBC //p' | sed 's/).*//'"
				OUTPUT_VARIABLE GLIBC_VERSION_STRING
				OUTPUT_STRIP_TRAILING_WHITESPACE
			)
			set(RELEASE_OS "linux-glibc${GLIBC_VERSION_STRING}")
		endif()
	elseif(APPLE)
		set(RELEASE_OS "mac")
	elseif(WIN32)
		set(RELEASE_OS "windows")
	else()
		set(RELEASE_OS ${CMAKE_SYSTEM_NAME})
	endif()
	
	if(CMAKE_OSX_ARCHITECTURES STREQUAL "arm64")
		set(RELEASE_ARCHITECTURE "arm64")
	elseif(CMAKE_OSX_ARCHITECTURES STREQUAL "x86_64")
		set(RELEASE_ARCHITECTURE "x64")
	elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "AMD64")
		set(RELEASE_ARCHITECTURE "x64")
	elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
		set(RELEASE_ARCHITECTURE "x64")
	else()
		set(RELEASE_ARCHITECTURE ${CMAKE_SYSTEM_PROCESSOR})
	endif()
	
	set(RELEASE_NAME "ccc_${RELEASE_VERSION}_${RELEASE_OS}_${RELEASE_ARCHITECTURE}")
	add_custom_target(releasezip ALL
		COMMAND
			${CMAKE_COMMAND} -E make_directory "${CMAKE_BINARY_DIR}/${RELEASE_NAME}" &&
			${CMAKE_COMMAND} -E copy ${RELEASE_CONTENTS} "${CMAKE_BINARY_DIR}/${RELEASE_NAME}" &&
			${CMAKE_COMMAND} -E tar "cfv" "${RELEASE_NAME}.zip" --format=zip ${CMAKE_BINARY_DIR}/"${RELEASE_NAME}"
		WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
	)
endif()

cmake_minimum_required(VERSION 2.6)

ADD_DEFINITIONS(-I..)
INCLUDE_DIRECTORIES(${CMAKE_CURRENT_SOURCE_DIR}/..)

IF (BUILD_EXAMPLES)
	ADD_EXECUTABLE(server server.c count.c)

	INCLUDE(FindPkgConfig)
	pkg_search_module(JANSSON jansson)
	find_path(JANSSON_INCLUDE_DIRS
	  NAMES jansson.h
	  HINTS ${LOCAL_JANSSON_INCLUDE_DIRS}
	  )
	find_library(JANSSON_LIBRARIES
	  NAMES jansson
	  HINTS ${LOCAL_JANSSON_LIBRARY_DIRS}
	  )
	if(JANSSON_FOUND)
	  ADD_DEFINITIONS(-DJANSSON)
	  INCLUDE_DIRECTORIES(${JANSSON_INCLUDE_DIRS})
	else()
	  error("jansson not found")
	endif()
	TARGET_LINK_LIBRARIES(server ubus ${ubox_library} ${blob_library} ${JANSSON_LIBRARIES})

	ADD_EXECUTABLE(client client.c count.c)
	TARGET_LINK_LIBRARIES(client ubus ${ubox_library})
ENDIF()

if (USE_BRAW AND BRAW_FOUND)
	message (STATUS "Building braw plugin")
	add_oiio_plugin (brawinput.cpp BlackmagicRawAPIDispatch.cpp
		INCLUDE_DIRS ${BRAW_INCLUDE_DIR}
		LINK_LIBRARIES ${BRAW_LIBRARIES}
		DEFINITIONS "-DUSE_BRAW=1")
else ()
	message (WARNING "braw plugin will not be built")
endif ()


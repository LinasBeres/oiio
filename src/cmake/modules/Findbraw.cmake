# - Find braw
# Find the LibRaw library <http://www.libraw.org>
# This module defines
#  BRAW_VERSION_STRING, the version string of braw
#  BRAW_INCLUDE_DIR, where to find the BlackmagicRAWAPI.h
#  BRAW_LIBRARIES, the libraries needed to use braw
#  BRAW_DEFINITIONS, the definitions needed to use braw
#
# Copyright (c) 2020, Linas Beresna <beres dot linas at gmail dot com>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

FIND_PACKAGE(PkgConfig)

IF(PKG_CONFIG_FOUND AND NOT BRAW_PATH)
	PKG_CHECK_MODULES(PC_BRAW QUIET braw)
	SET(BRAW_DEFINITIONS ${PC_LIBRAW_CFLAGS_OTHER})

ENDIF()

FIND_PATH(BRAW_INCLUDE_DIR BlackmagicRawAPI.h LinuxCOM.h
	HINTS
	${BRAW_INCLUDEDIR_HINT}
	${BRAW_PATH}
	${BRAW_INCLUDE_PATH}
	${PC_BRAW_INCLUDEDIR}
	${PC_BRAW_INCLUDE_DIRS}
	PATH_SUFFIXES libraw
	)

FIND_LIBRARY(BRAW_LIBRARIES NAMES braw	libBlackmagicRawAPI BlackmagicRawAPI
	HINTS
	${BRAW_LIBDIR_HINT}
	${BRAW_PATH}
	${PC_BRAW_LIBDIR}
	${PC_BRAW_LIBRARY_DIRS}
	)

message (STATUS "BRAW_LIBRARIES: ${BRAW_LIBRARIES}" )
message (STATUS "BRAW_INCLUDE: ${BRAW_INCLUDE_DIR}" )

# IF(LibRaw_INCLUDE_DIR)
# FILE(READ ${LibRaw_INCLUDE_DIR}/libraw/libraw_version.h _libraw_version_content)
#
# STRING(REGEX MATCH "#define LIBRAW_MAJOR_VERSION[ \t]*([0-9]*)\n" _version_major_match ${_libraw_version_content})
# SET(_libraw_version_major "${CMAKE_MATCH_1}")
#
# STRING(REGEX MATCH "#define LIBRAW_MINOR_VERSION[ \t]*([0-9]*)\n" _version_minor_match ${_libraw_version_content})
# SET(_libraw_version_minor "${CMAKE_MATCH_1}")
#
# STRING(REGEX MATCH "#define LIBRAW_PATCH_VERSION[ \t]*([0-9]*)\n" _version_patch_match ${_libraw_version_content})
# SET(_libraw_version_patch "${CMAKE_MATCH_1}")
#
# IF(_version_major_match AND _version_minor_match AND _version_patch_match)
# SET(LibRaw_VERSION_STRING "${_libraw_version_major}.${_libraw_version_minor}.${_libraw_version_patch}")
# ELSE()
# IF(NOT LibRaw_FIND_QUIETLY)
# MESSAGE(STATUS "Failed to get version information from ${LibRaw_INCLUDE_DIR}/libraw/libraw_version.h")
# ENDIF()
# ENDIF()
# ENDIF()
#
INCLUDE(FindPackageHandleStandardArgs)
FIND_PACKAGE_HANDLE_STANDARD_ARGS(braw
	REQUIRED_VARS BRAW_LIBRARIES BRAW_INCLUDE_DIR
	VERSION_VAR BRAW_VERSION_STRING
	)

MARK_AS_ADVANCED(BRAW_VERSION_STRING
	BRAW_INCLUDE_DIR
	BRAW_LIBRARIES
	BRAW_DEFINITIONS
	)

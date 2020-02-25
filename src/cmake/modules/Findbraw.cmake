# - Find braw
# Find the LibRaw library <http://www.libraw.org>
# This module defines
#  BRAW_INCLUDE_DIR, where to find the BlackmagicRAWAPI.h
#  BRAW_LIBRARIES, the libraries needed to use braw
#  BRAW_DEFINITIONS, the definitions needed to use braw
#
# TODO Change this to be more in line with DNEG
# Copyright (c) 2020, libe at dneg dot come
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

# TODO In general I am sure this cmake file can be tidied up and more information about the library could
# be obtained, such as versioning info and so forth.

FIND_PACKAGE(PkgConfig)

IF(PKG_CONFIG_FOUND AND NOT BRAW_PATH)
    PKG_CHECK_MODULES(PC_BRAW QUIET braw)
    SET(BRAW_DEFINITIONS ${PC_LIBRAW_CFLAGS_OTHER})

ENDIF()

FIND_PATH(BRAW_INCLUDE_DIR BlackmagicRawAPI.h   # LinuxCOM.h must also be present
    HINTS
    ${BRAW_INCLUDEDIR_HINT}
    ${BRAW_PATH}
    ${BRAW_INCLUDE_PATH}
    ${PC_BRAW_INCLUDEDIR}
    ${PC_BRAW_INCLUDE_DIRS}
    PATH_SUFFIXES libraw
    )

# BlackmagicRAW also requires libc++ and libc++abi

FIND_LIBRARY(BRAW_LIBRARIES NAMES braw libBlackmagicRawAPI BlackmagicRawAPI
    HINTS
    ${BRAW_LIBDIR_HINT}
    ${BRAW_PATH}
    ${PC_BRAW_LIBDIR}
    ${PC_BRAW_LIBRARY_DIRS}
    )

# BlackmagicRAW doesn't seem to have information about versioning...
# Will have to continue to check their website to see if there are any versioning going on

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

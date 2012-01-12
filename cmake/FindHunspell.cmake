# - Try to find the HUNSPELL libraries
# Once done this will define
#
#  HUNSPELL_FOUND - system has HUNSPELL
#  HUNSPELL_INCLUDE_DIR - the HUNSPELL include directory
#  HUNSPELL_LIBRARIES - HUNSPELL library
#
# Copyright (c) 2012 CSSlayer <wengxt@gmail.com>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if(HUNSPELL_INCLUDE_DIR AND HUNSPELL_LIBRARIES)
    # Already in cache, be silent
    set(HUNSPELL_FIND_QUIETLY TRUE)
endif(HUNSPELL_INCLUDE_DIR AND HUNSPELL_LIBRARIES)

find_package(PkgConfig)
pkg_check_modules(HUNSPELL QUIET hunspell)

find_path(HUNSPELL_MAIN_INCLUDE_DIR
          NAMES hunspell.h
          HINTS ${HUNSPELL_INCLUDEDIR}
          PATH_SUFFIXES hunspell)

find_library(HUNSPELL_LIBRARIES
             NAMES hunspell
             HINTS ${HUNSPELL_LIBDIR})

_pkgconfig_invoke("hunspell" HUNSPELL DATADIR "" "--variable=datadir")

set(HUNSPELL_INCLUDE_DIR "${HUNSPELL_MAIN_INCLUDE_DIR}")

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(HUNSPELL  DEFAULT_MSG  HUNSPELL_LIBRARIES HUNSPELL_MAIN_INCLUDE_DIR)

mark_as_advanced(HUNSPELL_INCLUDE_DIR HUNSPELL_LIBRARIES)

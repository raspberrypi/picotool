# - Try to find the libusb library
# Once done this defines
#
#  LIBUSB_FOUND - system has libusb
#  LIBUSB_INCLUDE_DIR - the libusb include directory
#  LIBUSB_LIBRARIES - Link these to use libusb
# Copyright (c) 2006, 2008  Laurent Montel, <montel@kde.org>
#
# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.
if (LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARIES)
    # in cache already
    set(LIBUSB_FOUND TRUE)
else (LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARIES)
    # use pkg-config to get the directories and then use these values
    # in the find_path() and find_library() calls. Might fail, pkg-config
    # might not be installed, e.g. for Windows systems
    if (PKG_CONFIG_FOUND)
        find_package(PkgConfig)
        pkg_check_modules(PC_LIBUSB libusb-1.0)
    else ()
        # As the pkg-config was not found we are probably building under windows.
        # Determine the architecture of the host, to choose right library
        if (NOT DEFINED ARCHITECTURE)
            if (CMAKE_SIZEOF_VOID_P GREATER 4)
                set(ARCHITECTURE 64)
            else()
                set(ARCHITECTURE 32)
            endif()
        endif()

        find_path(LIBUSB_INCLUDE_DIR libusb.h
            HINTS $ENV{LIBUSB_ROOT}/include/libusb-1.0
            PATHS ${PC_LIBUSB_INCLUDEDIR} ${PC_LIBUSB_INCLUDE_DIRS})

        find_library(LIBUSB_LIBRARIES NAMES libusb-1.0 usb-1.0 usb
            if (MINGW OR CYGWIN)
                HINTS $ENV{LIBUSB_ROOT}/MinGW${ARCHITECTURE}/static
            elseif(MSVC)
                HINTS $ENV{LIBUSB_ROOT}/VS2019/MS${ARCHITECTURE}/static
            endif ()
            PATHS ${PC_LIBUSB_LIBDIR} ${PC_LIBUSB_LIBRARY_DIRS})
    endif ()

    include(FindPackageHandleStandardArgs)

    FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBUSB DEFAULT_MSG LIBUSB_LIBRARIES LIBUSB_INCLUDE_DIR)
    MARK_AS_ADVANCED(LIBUSB_INCLUDE_DIR LIBUSB_LIBRARIES)
endif (LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARIES)

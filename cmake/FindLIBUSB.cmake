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

# Set LIBUSB_ROOT if specified
if (LIBUSB_ROOT)
    set(ENV{LIBUSB_ROOT} ${LIBUSB_ROOT})
endif()

if (LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARIES)
    # in cache already
    set(LIBUSB_FOUND TRUE)
else (LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARIES)
    # use pkg-config to get the directories and then use these values
    # in the find_path() and find_library() calls. Might fail, pkg-config
    # might not be installed, e.g. for Windows systems
    find_package(PkgConfig)

    if (PKG_CONFIG_FOUND)
        pkg_check_modules(PC_LIBUSB libusb-1.0)
    endif()

    if (NOT PC_LIBUSB_FOUND)
        # As the pkg-config was not found we are probably building under windows.
        # Determine the architecture of the host, to choose right library
        if (NOT DEFINED ARCHITECTURE)
            if (CMAKE_SIZEOF_VOID_P GREATER 4)
                set(ARCHITECTURE 64)
            else()
                set(ARCHITECTURE 32)
            endif()
        endif()

        set(PC_LIBUSB_INCLUDEDIR_HINT $ENV{LIBUSB_ROOT}/include)

        if (MINGW OR CYGWIN)
            set(PC_LIBUSB_LIBDIR_HINT $ENV{LIBUSB_ROOT}/MinGW${ARCHITECTURE}/static)
        elseif(MSVC)
            set(PC_LIBUSB_LIBDIR_HINT $ENV{LIBUSB_ROOT}/VS2019/MS${ARCHITECTURE}/static)
        endif ()
    endif ()

    find_path(LIBUSB_INCLUDE_DIR libusb.h
        HINTS ${PC_LIBUSB_INCLUDEDIR_HINT}
        PATHS ${PC_LIBUSB_INCLUDEDIR} ${PC_LIBUSB_INCLUDE_DIRS}
        PATH_SUFFIXES libusb-1.0)
        
    find_library(LIBUSB_LIBRARIES NAMES libusb-1.0 usb-1.0 usb
        HINTS ${PC_LIBUSB_LIBDIR_HINT}
        PATHS ${PC_LIBUSB_LIBDIR} ${PC_LIBUSB_LIBRARY_DIRS})

    include(FindPackageHandleStandardArgs)
    FIND_PACKAGE_HANDLE_STANDARD_ARGS(LIBUSB DEFAULT_MSG LIBUSB_LIBRARIES LIBUSB_INCLUDE_DIR)

    # Don't use .dll.a libraries, as they require the .dll file to be in the correct location
    # Replace with .a for static linking instead
    string(REPLACE ".dll.a" ".a" LIBUSB_LIBRARIES ${LIBUSB_LIBRARIES})

    MARK_AS_ADVANCED(LIBUSB_INCLUDE_DIR LIBUSB_LIBRARIES)
endif (LIBUSB_INCLUDE_DIR AND LIBUSB_LIBRARIES)

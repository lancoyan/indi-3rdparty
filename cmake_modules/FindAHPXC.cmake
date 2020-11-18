# - Try to find AHPXC Universal Library
# Once done this will define
#
#  AHPXC_FOUND - system has AHPXC
#  AHPXC_INCLUDE_DIR - the AHPXC include directory
#  AHPXC_LIBRARIES - Link these to use AHPXC

# Redistribution and use is allowed according to the terms of the BSD license.
# For details see the accompanying COPYING-CMAKE-SCRIPTS file.

if (AHPXC_LIBRARIES)

      # in cache already
      set(AHPXC_FOUND TRUE)
      message(STATUS "Found libqsiapi: ${AHPXC_LIBRARIES}")

else (AHPXC_LIBRARIES)

      find_library(AHPXC_LIBRARIES NAMES ahp_xc
        PATHS
        ${_obLinkDir}
        ${GNUWIN32_DIR}/lib
      )

      if(AHPXC_LIBRARIES)
        set(AHPXC_FOUND TRUE)
      else (AHPXC_LIBRARIES)
        set(AHPXC_FOUND FALSE)
      endif(AHPXC_LIBRARIES)


      if (AHPXC_FOUND)
        if (NOT AHPXC_FIND_QUIETLY)
          message(STATUS "Found AHP XC: ${AHPXC_LIBRARIES}")
        endif (NOT AHPXC_FIND_QUIETLY)
      else (AHPXC_FOUND)
        if (AHPXC_FIND_REQUIRED)
          message(FATAL_ERROR "AHP XC not found. Please install libahp_xc http://www.indilib.org")
        endif (AHPXC_FIND_REQUIRED)
      endif (AHPXC_FOUND)

      mark_as_advanced(AHPXC_LIBRARIES)

endif (AHPXC_LIBRARIES)

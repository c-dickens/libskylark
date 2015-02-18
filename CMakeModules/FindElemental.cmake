#
# Find Elemental includes and library
#
# Elemental
# It can be found at:
#
# Elemental_INCLUDE_DIR - where to find H5hut.h
# Elemental_LIBRARY     - qualified libraries to link against.
# Elemental_FOUND       - do not attempt to use if "no" or undefined.

FIND_PATH(Elemental_INCLUDE_DIR elemental/core.hpp
  $ENV{ELEMENTAL_ROOT}/include
  $ENV{HOME}/Software/include
  /usr/local/include
  /usr/include
  NO_DEFAULT_PATH
)

FIND_LIBRARY(Elemental_LIBRARY elemental
   $ENV{ELEMENTAL_ROOT}/lib
   $ENV{HOME}/Software/lib
   /usr/local/lib
   /usr/lib
   NO_DEFAULT_PATH
)

FIND_LIBRARY(Pmrrr_LIBRARY pmrrr
   $ENV{ELEMENTAL_ROOT}/lib
   $ENV{HOME}/Software/lib
   /usr/local/lib
   /usr/lib
   NO_DEFAULT_PATH
)

IF(Elemental_INCLUDE_DIR AND Elemental_LIBRARY AND Pmrrr_LIBRARY)
  SET( Elemental_FOUND "YES")
ENDIF(Elemental_INCLUDE_DIR AND Elemental_LIBRARY AND Pmrrr_LIBRARY)

IF (Elemental_FOUND)
  IF (NOT Elemental_FIND_QUIETLY)
    MESSAGE(STATUS
            "Found Elemental:${Elemental_LIBRARY};${Elemental_LIBRARY_C}")
    MESSAGE(STATUS
            "Found pmrrr:${Pmrrr_LIBRARY}")
  ENDIF (NOT Elemental_FIND_QUIETLY)
ELSE (Elemental_FOUND)
  IF (Elemental_FIND_REQUIRED)
    MESSAGE(STATUS "Elemental not found!")
  ENDIF (Elemental_FIND_REQUIRED)
ENDIF (Elemental_FOUND)

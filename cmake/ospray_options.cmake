## Copyright 2009 Intel Corporation
## SPDX-License-Identifier: Apache-2.0

##############################################################
# Global configuration options
##############################################################

include(GNUInstallDirs)
include(CMakeDependentOption)

set(OSPRAY_CMAKECONFIG_DIR
    "${CMAKE_INSTALL_LIBDIR}/cmake/ospray-${OSPRAY_VERSION}")

set(ISPC_VERSION_REQUIRED 1.21.1)
set(RKCOMMON_VERSION_REQUIRED 1.12.0)
set(EMBREE_VERSION_REQUIRED 4.3.0)
set(OPENVKL_VERSION_REQUIRED 2.0.0)
set(OIDN_VERSION_REQUIRED 2.1.0)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR})

ospray_configure_build_type()
ospray_configure_compiler()

###########################################################
# OSPRay specific build options
###########################################################

option(OSPRAY_ENABLE_APPS "Enable the 'apps' subtree in the build." ON)
option(OSPRAY_ENABLE_TARGET_CLANGFORMAT
       "Enable 'format' target, requires clang-format too")
mark_as_advanced(OSPRAY_ENABLE_TARGET_CLANGFORMAT)

# Dependent options
cmake_dependent_option(
  OSPRAY_ENABLE_APPS_BENCHMARK
  "Enable building, installing, and packaging of benchmark tools."
  ON
  OSPRAY_ENABLE_APPS
  OFF
)

cmake_dependent_option(
  OSPRAY_ENABLE_APPS_EXAMPLES
  "Enable building, installing, and packaging of ospExamples."
  ON
  OSPRAY_ENABLE_APPS
  OFF
)

cmake_dependent_option(
  OSPRAY_ENABLE_APPS_TUTORIALS
  "Enable building, installing, and packaging of tutorial apps."
  ON
  OSPRAY_ENABLE_APPS
  OFF
)

cmake_dependent_option(
  OSPRAY_ENABLE_APPS_TESTING
  "Enable building, installing, and packaging of test tools."
  ON
  OSPRAY_ENABLE_APPS
  OFF
)

option(OSPRAY_ENABLE_VOLUMES "Enable volume rendering using OpenVKL." ON)


###########################################################
# OSPRay's dependencies and configuration selection
###########################################################

# rkcommon
find_package(rkcommon ${RKCOMMON_VERSION_REQUIRED} EXACT REQUIRED)
get_target_property(RKCOMMON_INCLUDE_DIRS rkcommon::rkcommon
  INTERFACE_INCLUDE_DIRECTORIES)

# Embree
ospray_find_embree(${EMBREE_VERSION_REQUIRED} FALSE)
ospray_verify_embree_features()

# ISPC
set_property(GLOBAL PROPERTY FIND_LIBRARY_USE_LIB64_PATHS ON)
find_package(ispcrt ${ISPC_VERSION_REQUIRED} REQUIRED)
set(ISPC_FAST_MATH ON)

# Open VKL
if (OSPRAY_ENABLE_VOLUMES)
  ospray_find_openvkl(${OPENVKL_VERSION_REQUIRED} FALSE)
  if (NOT openvkl_FOUND)
    message(FATAL_ERROR
            "We did not find Open VKL installed on your system. OSPRay requires"
            " an Open VKL installation >= v${OPENVKL_VERSION_REQUIRED}, please"
            " download and extract Open VKL (or compile from source), then"
            " set the 'openvkl_DIR' variable to the installation directory.")
  endif()
endif()

# OpenImageDenoise
if (OSPRAY_MODULE_DENOISER)
  find_package(OpenImageDenoise ${OIDN_VERSION_REQUIRED} REQUIRED)
endif()

# Configure OSPRay ISA last after we've detected what we got w/ Embree
ospray_configure_ispc_isa()
set(ISPC_TARGET_CPU ${OSPRAY_ISPC_TARGET_LIST})

#####################################################################
# Binary package options, before any install() invocation/definition
#####################################################################

option(OSPRAY_ZIP_MODE "Use tarball/zip CPack generator instead of RPM" ON)
mark_as_advanced(OSPRAY_ZIP_MODE)

option(OSPRAY_INSTALL_DEPENDENCIES
       "Install OSPRay dependencies in binary packages and install")
mark_as_advanced(OSPRAY_INSTALL_DEPENDENCIES)

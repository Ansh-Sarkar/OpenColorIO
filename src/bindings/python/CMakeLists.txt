# SPDX-License-Identifier: BSD-3-Clause
# Copyright Contributors to the OpenColorIO Project.

###############################################################################
# External dependencies

find_package(Python REQUIRED COMPONENTS Interpreter Development)

###############################################################################
# PyDoc generation
# file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/include")
# set(PYDOC_OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/include/PyDoc.h")
# add_custom_command(OUTPUT ${PYDOC_OUTPUT}
# 	COMMAND ${Python_EXECUTABLE} createPyDocH.py ${PYDOC_OUTPUT}
# 	WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}
# )

###############################################################################
# Python libs

set(SOURCES
	PyAllocationTransform.cpp
	PyBaker.cpp
	PyBuiltinTransform.cpp
	PyBuiltinTransformRegistry.cpp
	PyCDLTransform.cpp
	PyColorSpace.cpp
	PyColorSpaceSet.cpp
	PyColorSpaceTransform.cpp
	PyConfig.cpp
	PyContext.cpp
	PyCPUProcessor.cpp
	PyDisplayViewTransform.cpp
	PyDynamicProperty.cpp
	PyExponentTransform.cpp
	PyExponentWithLinearTransform.cpp
	PyExposureContrastTransform.cpp
	PyFileRules.cpp
	PyFileTransform.cpp
	PyFixedFunctionTransform.cpp
	PyFormatMetadata.cpp
	PyGPUProcessor.cpp
	PyGpuShaderCreator.cpp
	PyGpuShaderDesc.cpp
	PyGradingData.cpp
	PyGradingPrimaryTransform.cpp
	PyGradingRGBCurveTransform.cpp
	PyGradingToneTransform.cpp
	PyGroupTransform.cpp
	PyImageDesc.cpp
	PyLogAffineTransform.cpp
	PyLogCameraTransform.cpp
	PyLogTransform.cpp
	PyLook.cpp
	PyLookTransform.cpp
	PyLut1DTransform.cpp
	PyLut3DTransform.cpp
	PyMatrixTransform.cpp
	PyNamedTransform.cpp
	PyOpenColorIO.cpp
	PyPackedImageDesc.cpp
	PyPlanarImageDesc.cpp
	PyProcessor.cpp
	PyProcessorMetadata.cpp
	PyRangeTransform.cpp
	PySystemMonitors.cpp
	PyTransform.cpp
	PyTypes.cpp
	PyUtils.cpp
	PyViewingRules.cpp
	PyViewTransform.cpp
)

add_library(PyOpenColorIO MODULE ${SOURCES} ${PYDOC_OUTPUT})
set_target_properties(PyOpenColorIO PROPERTIES
	PREFIX ""
)

if(WIN32)
	# Windows uses .pyd extension for python modules
	set_target_properties(PyOpenColorIO PROPERTIES
		SUFFIX ".pyd"
	)
endif()

# NOTE: Depending of the compiler version pybind11 2.4.3 does not compile with C++17 so revert to c++11

set(APP_CXX_STANDARD ${CMAKE_CXX_STANDARD})
if(${CMAKE_CXX_STANDARD} GREATER_EQUAL 17)
	set(APP_CXX_STANDARD 11)
endif()

set_target_properties(PyOpenColorIO
	PROPERTIES
		COMPILE_FLAGS "${PLATFORM_COMPILE_FLAGS}"
		CXX_STANDARD ${APP_CXX_STANDARD}
)

if(NOT BUILD_SHARED_LIBS)
	target_compile_definitions(PyOpenColorIO
		PRIVATE
			OpenColorIO_SKIP_IMPORTS
	)
endif()

if(UNIX)
	# A 'module' is a dynamic library on Linux (i.e. '-fPIC' needed),
	# but a static library on Windows.

	# If supported for the target machine, emit position-independent code
	# suitable for dynamic linking.
	set_property(TARGET PyOpenColorIO PROPERTY POSITION_INDEPENDENT_CODE ON)
endif()

# OSX demands that the linker resolve all symbols at build time
# we pass this flag to allow dynamic linking
if(APPLE)
	set_target_properties(PyOpenColorIO PROPERTIES
		LINK_FLAGS "-undefined dynamic_lookup"
	)
endif()

target_include_directories(PyOpenColorIO
	PRIVATE
		PyOpenColorIO
		${CMAKE_CURRENT_BINARY_DIR}/include
		${CMAKE_CURRENT_SOURCE_DIR}
		${pybind11_INCLUDE_DIR}
	SYSTEM
		${Python_INCLUDE_DIRS}
)

# pybind11 recommends intentionally NOT linking against libpython on Linux and 
# macOS, to prevent segfaults when potentially working with multiple Python 
# installations. See note in:
#   https://pybind11.readthedocs.io/en/stable/compiling.html#building-manually
set(PYOCIO_LINK_PUBLIC public_api)
if(WIN32)
	list(INSERT PYOCIO_LINK_PUBLIC 0 ${Python_LIBRARIES})
endif()

target_link_libraries(PyOpenColorIO
	PUBLIC
		${PYOCIO_LINK_PUBLIC}
	PRIVATE
		OpenColorIO
		utils::strings
		pybind11::module
)

target_compile_definitions(PyOpenColorIO
	PRIVATE
		PYOCIO_NAME=PyOpenColorIO
)

if(WIN32)
    set(_Python_VARIANT_PATH "lib${LIB_SUFFIX}/site-packages")
else()
    set(_Python_VARIANT_PATH "lib${LIB_SUFFIX}/python${Python_VERSION_MAJOR}.${Python_VERSION_MINOR}/site-packages")
endif()

install(TARGETS PyOpenColorIO
    LIBRARY DESTINATION ${_Python_VARIANT_PATH}
)

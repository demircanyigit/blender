# SPDX-FileCopyrightText: 2011-2020 the Polygon Mesh Processing Library developers
# SPDX-FileCopyrightText: 2026 Blender Authors (build integration only)
#
# SPDX-License-Identifier: MIT
#
# Polygon Mesh Processing Library (https://github.com/pmp-library/pmp-library), vendored under
# its MIT-style license. Only the parts needed for remeshing are included, see README.blender.

set(INC
  PUBLIC src
)

set(INC_SYS
)

set(SRC
  src/pmp/surface_mesh.cpp
  src/pmp/algorithms/curvature.cpp
  src/pmp/algorithms/decimation.cpp
  src/pmp/algorithms/differential_geometry.cpp
  src/pmp/algorithms/distance_point_triangle.cpp
  src/pmp/algorithms/features.cpp
  src/pmp/algorithms/laplace.cpp
  src/pmp/algorithms/normals.cpp
  src/pmp/algorithms/numerics.cpp
  src/pmp/algorithms/remeshing.cpp

  src/pmp/bounding_box.h
  src/pmp/exceptions.h
  src/pmp/mat_vec.h
  src/pmp/properties.h
  src/pmp/surface_mesh.h
  src/pmp/types.h
  src/pmp/algorithms/barycentric_coordinates.h
  src/pmp/algorithms/curvature.h
  src/pmp/algorithms/decimation.h
  src/pmp/algorithms/differential_geometry.h
  src/pmp/algorithms/distance_point_triangle.h
  src/pmp/algorithms/features.h
  src/pmp/algorithms/laplace.h
  src/pmp/algorithms/normals.h
  src/pmp/algorithms/numerics.h
  src/pmp/algorithms/remeshing.h
  src/pmp/io/io_flags.h
)

set(LIB
  PUBLIC bf::dependencies::eigen
  PRIVATE bf::dependencies::optional::tbb
)

# The library relies on M_PI, which is not defined by <cmath> on MSVC without this.
if(WIN32)
  add_definitions(-D_USE_MATH_DEFINES -DNOMINMAX)
endif()

blender_add_lib(extern_pmp "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
add_library(bf::extern::pmp ALIAS extern_pmp)

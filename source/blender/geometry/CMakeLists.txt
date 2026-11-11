# SPDX-FileCopyrightText: 2006 Blender Authors
#
# SPDX-License-Identifier: GPL-2.0-or-later

set(INC
  PUBLIC .
  ../makesrna
  ../../../intern/eigen
)

set(INC_SYS
)

set(SRC
  intern/add_curves_on_mesh.cc
  intern/curve_constraints.cc
  intern/curves_remove_and_split.cc
  intern/extend_curves.cc
  intern/extract_elements.cc
  intern/fillet_curves.cc
  intern/fit_curves.cc
  intern/foreach_geometry.cc
  intern/interpolate_curves.cc
  intern/isotropic_remesh.cc
  intern/quadriflow_remesh.cc
  intern/axis_deform.cc
  intern/curve_modifiers.cc
  intern/mesh_cap_holes.cc
  intern/mesh_quadrangulate.cc
  intern/mesh_elements.cc
  intern/mesh_topology_ops.cc
  intern/deform_ops.cc
  intern/curve_sweep.cc
  intern/uv_smart_project.cc
  intern/mesh_channels.cc
  intern/mesh_face_extrude.cc
  intern/radial_array.cc
  intern/uvw_map.cc
  intern/uv_scale.cc
  intern/mesh_divide.cc
  intern/mesh_blast.cc
  intern/volume_select.cc
  intern/join_geometries.cc
  intern/merge_curves.cc
  intern/merge_layers.cc
  intern/mesh_decimate.cc
  intern/decimate_backends.cc
  intern/decimate_cgal.cc
  intern/decimate_wild.cc
  intern/decimate_backend_types.hh
  intern/mesh_clip.cc
  intern/mesh_collapse.cc
  intern/mesh_dice.cc
  intern/edge_damage.cc
  intern/mesh_dissolve.cc
  intern/mesh_drape.cc
  intern/mesh_boards.cc
  intern/mesh_boards.hh
  intern/mesh_masonry.cc
  intern/mesh_planks.cc
  intern/mesh_roof.cc
  intern/mesh_carve.cc
  intern/mesh_carve_layout.cc
  intern/mesh_cleanup.cc
  intern/mesh_stonework.cc
  intern/mesh_distribute.cc
  intern/mesh_materialize.cc
  intern/mesh_shatter.cc
  intern/mesh_sim_cache.cc
  intern/mesh_smooth.cc
  intern/mesh_voronoi_fracture.cc
  intern/pmp_mesh_conversion.hh
  intern/mesh_boolean.cc
  intern/mesh_boolean_manifold.cc
  intern/mesh_copy_selection.cc
  intern/mesh_merge_by_distance.cc
  intern/mesh_primitive_cuboid.cc
  intern/mesh_primitive_cylinder_cone.cc
  intern/mesh_primitive_grid.cc
  intern/mesh_primitive_line.cc
  intern/mesh_primitive_uv_sphere.cc
  intern/mesh_selection.cc
  intern/mesh_split_edges.cc
  intern/mesh_to_curve_convert.cc
  intern/mesh_to_volume.cc
  intern/mesh_triangulate.cc
  intern/mix_geometries.cc
  intern/point_merge_by_distance.cc
  intern/points_to_volume.cc
  intern/randomize.cc
  intern/realize_instances.cc
  intern/reorder.cc
  intern/resample_curves.cc
  intern/reverse_uv_sampler.cc
  intern/separate_geometry.cc
  intern/set_curve_type.cc
  intern/simplify_curves.cc
  intern/smooth_curves.cc
  intern/subdivide_curves.cc
  intern/transform.cc
  intern/trim_curves.cc
  intern/uv_pack.cc
  intern/uv_parametrizer.cc
  intern/volume_grid_resample.cc

  GEO_add_curves_on_mesh.hh
  GEO_curve_constraints.hh
  GEO_curves_remove_and_split.hh
  GEO_extend_curves.hh
  GEO_extract_elements.hh
  GEO_fillet_curves.hh
  GEO_fit_curves.hh
  GEO_foreach_geometry.hh
  GEO_interpolate_curves.hh
  GEO_isotropic_remesh.hh
  GEO_quadriflow_remesh.hh
  GEO_axis_deform.hh
  GEO_curve_modifiers.hh
  GEO_mesh_cap_holes.hh
  GEO_mesh_quadrangulate.hh
  GEO_mesh_elements.hh
  GEO_mesh_topology_ops.hh
  GEO_deform_ops.hh
  GEO_curve_sweep.hh
  GEO_uv_smart_project.hh
  GEO_mesh_channels.hh
  GEO_mesh_face_extrude.hh
  GEO_radial_array.hh
  GEO_uvw_map.hh
  GEO_uv_scale.hh
  GEO_mesh_divide.hh
  GEO_mesh_blast.hh
  GEO_decimate_backends.hh
  GEO_volume_select.hh
  GEO_join_geometries.hh
  GEO_merge_curves.hh
  GEO_merge_layers.hh
  GEO_mesh_decimate.hh
  GEO_mesh_clip.hh
  GEO_mesh_collapse.hh
  GEO_mesh_dice.hh
  GEO_edge_damage.hh
  GEO_mesh_dissolve.hh
  GEO_mesh_drape.hh
  GEO_mesh_masonry.hh
  GEO_mesh_planks.hh
  GEO_mesh_roof.hh
  GEO_mesh_carve.hh
  GEO_mesh_cleanup.hh
  GEO_mesh_stonework.hh
  GEO_mesh_distribute.hh
  GEO_mesh_materialize.hh
  GEO_mesh_shatter.hh
  GEO_mesh_sim_cache.hh
  GEO_mesh_smooth.hh
  GEO_mesh_voronoi_fracture.hh
  GEO_mesh_boolean.hh
  GEO_mesh_copy_selection.hh
  GEO_mesh_merge_by_distance.hh
  GEO_mesh_primitive_cuboid.hh
  GEO_mesh_primitive_cylinder_cone.hh
  GEO_mesh_primitive_grid.hh
  GEO_mesh_primitive_line.hh
  GEO_mesh_primitive_uv_sphere.hh
  GEO_mesh_selection.hh
  GEO_mesh_split_edges.hh
  GEO_mesh_to_curve.hh
  GEO_mesh_to_volume.hh
  GEO_mesh_triangulate.hh
  GEO_mix_geometries.hh
  GEO_point_merge_by_distance.hh
  GEO_points_to_volume.hh
  GEO_randomize.hh
  GEO_realize_instances.hh
  GEO_reorder.hh
  GEO_resample_curves.hh
  GEO_reverse_uv_sampler.hh
  GEO_separate_geometry.hh
  GEO_set_curve_type.hh
  GEO_simplify_curves.hh
  GEO_smooth_curves.hh
  GEO_subdivide_curves.hh
  GEO_transform.hh
  GEO_trim_curves.hh
  GEO_uv_pack.hh
  GEO_uv_parametrizer.hh
  GEO_volume_grid_resample.hh
  intern/mesh_boolean_intern.hh
  intern/mesh_boolean_manifold.hh
)

set(LIB
  PRIVATE bf::blenkernel
  PRIVATE bf::blenlib
  PRIVATE bf::blentranslation
  PRIVATE bf::bmesh
  PRIVATE bf::dna
  PRIVATE bf::functions
  PRIVATE bf::nodes
  PRIVATE bf::intern::atomic
  PRIVATE bf::intern::guardedalloc
  PRIVATE bf::extern::curve_fit_nd
  PRIVATE bf::extern::xxhash
  PRIVATE bf::extern::pmp
  PRIVATE bf::extern::meshoptimizer
  PRIVATE bf::dependencies::eigen
  PRIVATE bf::dependencies::optional::manifold
  PRIVATE bf::dependencies::optional::gmp
)

if(WITH_UV_SLIM)
  list(APPEND INC
    ../../../intern/slim
  )
  add_definitions(-DWITH_UV_SLIM)
endif()

if(WITH_BULLET)
  list(APPEND INC
    ../../../intern/rigidbody
  )
  list(APPEND LIB
    bf_intern_rigidbody
    PRIVATE bf::dependencies::optional::bullet
  )
endif()

if(WITH_MANIFOLD)
  add_definitions(-DWITH_MANIFOLD)
endif()

if(WITH_QUADRIFLOW)
  add_definitions(-DWITH_QUADRIFLOW)
endif()

blender_add_lib(bf_geometry "${SRC}" "${INC}" "${INC_SYS}" "${LIB}")
if(WITH_DECIMATE_CGAL)
  set(DECIMATE_DEPS_DIR "${CMAKE_SOURCE_DIR}/lib/decimation" CACHE PATH "CGAL and Boost header dependencies")
  if(NOT EXISTS "${DECIMATE_DEPS_DIR}/cgal/include/CGAL/Surface_mesh.h" OR
     NOT EXISTS "${DECIMATE_DEPS_DIR}/boost/boost/config.hpp")
    message(FATAL_ERROR "CGAL Decimate dependencies missing. Run: python build_files/utils/make_decimate_deps.py")
  endif()
  target_include_directories(bf_geometry SYSTEM PRIVATE
    "${DECIMATE_DEPS_DIR}/cgal/include" "${DECIMATE_DEPS_DIR}/boost")
  target_compile_definitions(bf_geometry PRIVATE WITH_DECIMATE_CGAL CGAL_EIGEN3_ENABLED BOOST_ALL_NO_LIB)
  if(MSVC)
    set_source_files_properties(intern/decimate_cgal.cc PROPERTIES COMPILE_OPTIONS "/bigobj;/fp:strict")
  endif()
endif()
add_library(bf::geometry ALIAS bf_geometry)

if(WITH_GTESTS)
  set(TEST_INC
  )
  set(TEST_SRC
    tests/GEO_interpolate_curves_test.cc
    tests/GEO_isotropic_remesh_test.cc
    tests/GEO_merge_curves_test.cc
    tests/GEO_realize_instances_test.cc
  )
  set(TEST_LIB
    PRIVATE bf::intern::clog
  )
  blender_add_test_suite_lib(bf_geometry_tests "${TEST_SRC}" "${INC};${TEST_INC}" "${INC_SYS}" "${LIB};${TEST_LIB}")
endif()

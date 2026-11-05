/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <algorithm>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_math_geom.h"
#include "BLI_math_vector.hh"

#include "BKE_mesh.hh"
#include "BKE_mesh_remesh_voxel.hh"

#include "GEO_quadriflow_remesh.hh"
#include "GEO_randomize.hh"

namespace blender::geometry {

#ifdef WITH_QUADRIFLOW

/** QuadriFlow can crash on anything but a consistent manifold, so it's checked first. */
static bool mesh_is_supported(const Mesh &mesh)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int2> edges = mesh.edges();
  const Span<int> corner_verts = mesh.corner_verts();
  const Span<int> corner_edges = mesh.corner_edges();

  Array<int8_t> edge_face_counts(mesh.edges_num, 0);
  Array<int> edge_first_verts(mesh.edges_num, -1);
  for (const int corner : corner_verts.index_range()) {
    const int edge = corner_edges[corner];
    edge_face_counts[edge]++;
    if (edge_face_counts[edge] > 2) {
      return false;
    }
    /* Neighboring faces have to go around in the same direction. */
    if (edge_first_verts[edge] == corner_verts[corner]) {
      return false;
    }
    edge_first_verts[edge] = corner_verts[corner];
  }
  for (const int edge : edges.index_range()) {
    if (edge_face_counts[edge] == 0) {
      return false;
    }
    if (math::distance_squared(positions[edges[edge][0]], positions[edges[edge][1]]) < 1e-8f) {
      return false;
    }
  }
  return true;
}

static float mesh_area(const Mesh &mesh)
{
  const Span<float3> positions = mesh.vert_positions();
  const Span<int> corner_verts = mesh.corner_verts();
  float area = 0.0f;
  for (const int3 &tri : mesh.corner_tris()) {
    area += area_tri_v3(positions[corner_verts[tri[0]]],
                        positions[corner_verts[tri[1]]],
                        positions[corner_verts[tri[2]]]);
  }
  return area;
}

/** The remesher requires a progress callback; neither nodes nor modifiers can be stopped. */
static void update_callback(void * /*user_data*/, float /*progress*/, int *cancel)
{
  *cancel = 0;
}

Mesh *quadriflow_remesh(const Mesh &mesh,
                        const QuadriflowRemeshParams &params,
                        QuadriflowRemeshResult &r_result)
{
  if (mesh.faces_num == 0) {
    r_result = QuadriflowRemeshResult::Skipped;
    return nullptr;
  }
  if (!mesh_is_supported(mesh)) {
    r_result = QuadriflowRemeshResult::Unsupported;
    return nullptr;
  }
  int target_faces = params.face_count;
  if (params.target == QuadriflowTarget::Ratio) {
    target_faces = int(float(mesh.faces_num) * params.ratio);
  }
  else if (params.target == QuadriflowTarget::EdgeLength) {
    if (params.edge_length <= 0.0f) {
      r_result = QuadriflowRemeshResult::Skipped;
      return nullptr;
    }
    target_faces = int(
        std::min(mesh_area(mesh) / (params.edge_length * params.edge_length), 1e8f));
  }
  /* The remesher can't make a closed mesh with very few quads. */
  target_faces = std::max(target_faces, 6);

  Mesh *result = BKE_mesh_remesh_quadriflow(&mesh,
                                            target_faces,
                                            params.seed,
                                            params.preserve_sharp,
                                            params.preserve_boundary,
                                            false,
                                            update_callback,
                                            nullptr);
  if (result == nullptr) {
    r_result = QuadriflowRemeshResult::Failed;
    return nullptr;
  }
  BKE_mesh_copy_parameters_for_eval(result, &mesh);
  bke::mesh_remesh_reproject_attributes(mesh, *result);
  debug_randomize_mesh_order(result);
  r_result = QuadriflowRemeshResult::Success;
  return result;
}

#else

Mesh *quadriflow_remesh(const Mesh & /*mesh*/,
                        const QuadriflowRemeshParams & /*params*/,
                        QuadriflowRemeshResult &r_result)
{
  r_result = QuadriflowRemeshResult::Disabled;
  return nullptr;
}

#endif

}  // namespace blender::geometry

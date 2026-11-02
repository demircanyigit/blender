/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <cmath>
#include <exception>

#include "BLI_math_base.h"
#include "BLI_math_rotation.h"

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_mesh_decimate.hh"

#include "pmp_mesh_conversion.hh"

#include "pmp/algorithms/decimation.h"
#include "pmp/algorithms/features.h"

namespace blender::geometry {

static uint target_vertex_count(const pmp::SurfaceMesh &surface,
                                const VArray<bool> &selection,
                                const MeshDecimateParams &params)
{
  const double verts_num = double(surface.n_vertices());
  if (params.mode == MeshDecimateMode::FaceCount) {
    /* Removing a vertex removes two triangles, apart from on boundaries where it removes one. */
    const double faces_to_remove = double(surface.n_faces()) -
                                   double(std::max(params.face_count, 0));
    return uint(std::lround(std::clamp(verts_num - faces_to_remove / 2.0, 0.0, verts_num)));
  }

  double selected_num = verts_num;
  if (selection) {
    selected_num = 0.0;
    const auto selected = surface.get_vertex_property<bool>("v:selected");
    for (const pmp::Vertex vertex : surface.vertices()) {
      if (selected[vertex]) {
        selected_num += 1.0;
      }
    }
  }
  const double ratio = std::clamp(double(params.ratio), 0.0, 1.0);
  return uint(std::lround(verts_num - selected_num * (1.0 - ratio)));
}

Mesh *decimate_mesh(const Mesh &mesh,
                    const MeshDecimateParams &params,
                    const bke::AttributeFilter &attribute_filter,
                    int *r_detached_tris)
{
  pmp::SurfaceMesh surface;
  const int detached_tris = build_surface_mesh(mesh, params.hard_edges, params.selection, surface);
  if (r_detached_tris != nullptr) {
    *r_detached_tris = detached_tris;
  }
  if (surface.n_faces() == 0) {
    return nullptr;
  }

  if (params.preserve_boundary) {
    pmp::detect_boundary(surface);
  }

  bool has_selection = !params.selection;
  if (params.selection) {
    /* An empty selection would otherwise mean that all vertices can be removed. */
    auto selected = surface.get_vertex_property<bool>("v:selected");
    for (const pmp::Vertex vertex : surface.vertices()) {
      if (selected[vertex]) {
        has_selection = true;
        break;
      }
    }
  }

  if (has_selection) {
    try {
      pmp::decimate(surface,
                    target_vertex_count(surface, params.selection, params),
                    std::max(params.max_aspect_ratio, 0.0f),
                    0.0f,
                    0,
                    RAD2DEGF(std::max(params.max_normal_change, 0.0f)),
                    std::max(params.max_error, 0.0f));
    }
    catch (const std::exception &) {
      return nullptr;
    }
  }

  Mesh *result = surface_mesh_to_mesh(surface, mesh);
  if (result == nullptr) {
    return nullptr;
  }
  transfer_attributes_from_surface(mesh, *result, attribute_filter);
  return result;
}

}  // namespace blender::geometry

/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * Shared by the operations that are based on the Polygon Mesh Processing Library (extern/pmp).
 */

#include "BLI_virtual_array.hh"

#include "BKE_attribute_filter.hh"

#include "pmp/surface_mesh.h"

struct Mesh;

namespace blender::geometry {

/**
 * Build the triangle mesh for PMP, with \a hard_edges marked as feature edges and
 * \a vert_selection stored as the vertex selection. Both are optional.
 *
 * The half-edge data structure cannot represent non-manifold configurations. Such triangles are
 * attached with vertices of their own, which keeps the surface intact but leaves a seam.
 *
 * \return The number of triangles that had to be detached.
 */
int build_surface_mesh(const Mesh &mesh,
                       const VArray<bool> &hard_edges,
                       const VArray<bool> &vert_selection,
                       pmp::SurfaceMesh &surface);

/** \return A new mesh, or null if the surface has no faces. */
Mesh *surface_mesh_to_mesh(pmp::SurfaceMesh &surface, const Mesh &src_mesh);

/** Resample point, face and corner attributes onto a rebuilt mesh by nearest-surface sampling. */
void transfer_attributes_from_surface(const Mesh &src_mesh,
                                      Mesh &dst_mesh,
                                      const bke::AttributeFilter &attribute_filter,
                                      bool face_aware_corners = false);

}  // namespace blender::geometry

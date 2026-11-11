/* SPDX-FileCopyrightText: 2026 Blender Authors
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BKE_mesh.hh"
#include "BLI_math_euler.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "DNA_mesh_types.h"
#include "GEO_mesh_divide.hh"
#include "bmesh.hh"
#include "tools/bmesh_bisect_plane.hh"
#include <algorithm>
#include <cfloat>
#include <cmath>
#include <exception>
#include <limits>
#include <memory>
#include <vector>

namespace blender::geometry {
namespace {
constexpr int max_planes = 2048;
constexpr int max_elements = 2000000;
struct BMeshFree {
  void operator()(BMesh *mesh) const
  {
    if (mesh)
      BM_mesh_free(mesh);
  }
};
struct GridPlane {
  float3 normal;
  float distance;
};
bool finite(const float3 &value)
{
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}
double projection(const float3 &normal, const float3 &point)
{
  return double(normal.x) * point.x + double(normal.y) * point.y + double(normal.z) * point.z;
}
}  // namespace

Mesh *mesh_divide(const Mesh &mesh, const MeshDivideParams &params, std::string &message)
{
  if (mesh.verts_num == 0 || mesh.faces_num == 0)
    return nullptr;
  if (!finite(params.size) || !finite(params.offset) || !finite(params.rotation) ||
      !std::isfinite(params.tolerance) || params.tolerance < 0)
  {
    message = "Grid settings must be finite and tolerance must be non-negative";
    return nullptr;
  }
  if (params.size.x < 0 || params.size.y < 0 || params.size.z < 0) {
    message = "Grid Size cannot be negative; use zero to disable an axis";
    return nullptr;
  }
  const Span<float3> positions = mesh.vert_positions();
  for (const float3 &point : positions) {
    if (!finite(point)) {
      message = "Input contains non-finite vertex coordinates";
      return nullptr;
    }
  }
  const float tolerance = std::max(params.tolerance, 1e-7f);
  const float3x3 rotation = math::from_rotation<float3x3>(math::EulerXYZ(params.rotation));
  try {
    std::vector<GridPlane> planes;
    for (int axis = 0; axis < 3; axis++) {
      if (!params.axes[axis] || params.size[axis] == 0)
        continue;
      float3 unit(0);
      unit[axis] = 1;
      const float3 normal = math::normalize(rotation * unit);
      double low = std::numeric_limits<double>::infinity(), high = -low;
      double coordinate_scale = 0;
      for (const float3 &point : positions) {
        const double value = projection(normal, point);
        low = std::min(low, value);
        high = std::max(high, value);
        /* Include large terms that cancel after grid rotation: BMesh evaluates float dots. */
        coordinate_scale = std::max(coordinate_scale,
                                    std::abs(double(normal.x) * point.x) +
                                        std::abs(double(normal.y) * point.y) +
                                        std::abs(double(normal.z) * point.z));
      }
      if (high - low <= double(tolerance) * 2)
        continue;
      const double size = params.size[axis];
      const double precision = std::max(double(tolerance) * 2,
                                        double(FLT_EPSILON) * coordinate_scale * 4);
      if (size <= precision) {
        message = "Grid Size is too small for the tolerance or coordinate precision";
        return nullptr;
      }
      /* Phase is relative to the object origin, never to a changing bounding box. Reducing
       * offset modulo spacing avoids huge indices when users enter a distant grid offset. */
      const double phase = std::fmod(projection(normal, params.offset), size);
      const double first = std::ceil((low + tolerance - phase) / size);
      const double last = std::floor((high - tolerance - phase) / size);
      if (last < first)
        continue;
      const double count = last - first + 1;
      if (!std::isfinite(count) || count > max_planes - int(planes.size())) {
        message = "Grid needs more than 2048 cutting planes; increase Grid Size or disable axes";
        return nullptr;
      }
      for (int i = 0; i < int(count); i++) {
        const double distance = phase + (first + i) * size;
        if (!std::isfinite(distance) || std::abs(distance) > FLT_MAX) {
          message = "Grid plane is outside the supported coordinate range";
          return nullptr;
        }
        planes.push_back({normal, float(distance)});
      }
    }
    if (planes.empty())
      return nullptr;
    if (mesh.verts_num > max_elements || mesh.faces_num > max_elements ||
        mesh.corners_num > max_elements * 4)
    {
      message = "Input exceeds Divide's two-million-vertex/face evaluation budget";
      return nullptr;
    }
    BMeshCreateParams create{};
    create.use_toolflags = false;
    BMeshFromMeshParams convert{};
    convert.calc_face_normal = true;
    convert.calc_vert_normal = true;
    std::unique_ptr<BMesh, BMeshFree> bm(BKE_mesh_to_bmesh_ex(&mesh, &create, &convert));
    for (const GridPlane &plane : planes) {
      const float equation[4] = {plane.normal.x, plane.normal.y, plane.normal.z, -plane.distance};
      /* A single shared BMesh keeps neighboring polygons welded. Do not snap existing
       * vertices: tolerance classifies near-plane points without deforming the input. */
      BM_mesh_bisect_plane(bm.get(), equation, false, false, 0, 0, tolerance);
      if (bm->totvert > max_elements || bm->totface > max_elements ||
          bm->totloop > max_elements * 4)
      {
        message = "Divide exceeded its geometry budget; increase Grid Size";
        return nullptr;
      }
    }
    BM_mesh_normals_update(bm.get());
    return BKE_mesh_from_bmesh_for_eval_nomain(bm.get(), nullptr, &mesh);
  }
  catch (const std::exception &error) {
    message = std::string("Divide: ") + error.what();
    return nullptr;
  }
}
}  // namespace blender::geometry

/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>

#include "BLI_math_constants.h"
#include "BLI_math_matrix.hh"
#include "BLI_math_axis_angle.hh"
#include "BLI_math_rotation.hh"

#include "DNA_mesh_types.h"

#include "BKE_geometry_set.hh"
#include "BKE_instances.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_merge_by_distance.hh"
#include "GEO_radial_array.hh"
#include "GEO_realize_instances.hh"

namespace blender::geometry {

Mesh *radial_array(const Mesh &mesh, const RadialArrayParams &params)
{
  if (mesh.verts_num == 0) {
    return nullptr;
  }
  const int count = std::max(params.count, 1);
  const int axis = std::clamp(params.axis, 0, 2);
  float3 axis_dir(0.0f);
  axis_dir[axis] = 1.0f;
  float3 radial_dir(0.0f);
  radial_dir[(axis + 1) % 3] = 1.0f;

  /* A full turn spreads the copies evenly, otherwise the last copy sits at the end angle. */
  const bool full_turn = std::abs(std::abs(params.angle) - 2.0f * float(M_PI)) < 1e-4f;
  const float angle_step = (full_turn || count == 1) ? params.angle / count :
                                                       params.angle / (count - 1);

  std::unique_ptr<bke::Instances> instances = std::make_unique<bke::Instances>(count);
  const int handle = instances->add_reference(
      bke::InstanceReference{bke::GeometrySet::from_mesh(BKE_mesh_copy_for_eval(mesh))});
  MutableSpan<int> handles = instances->reference_handles_for_write();
  MutableSpan<float4x4> transforms = instances->transforms_for_write();

  for (const int i : IndexRange(count)) {
    const float theta = angle_step * i;
    const float4x4 rotation = math::from_rotation<float4x4>(math::AxisAngle(axis_dir, theta));
    const float3 offset = radial_dir * params.radius + axis_dir * (params.step * i);
    float4x4 transform;
    if (params.rotate_copies) {
      /* Move out, turn around the center, and put the center back. */
      transform = math::from_location<float4x4>(params.center) * rotation *
                  math::from_location<float4x4>(offset - params.center);
    }
    else {
      /* Only move each copy to where the turned copy would have its origin. */
      transform = math::from_location<float4x4>(
          params.center + math::transform_point(rotation, offset - params.center));
    }
    handles[i] = handle;
    transforms[i] = transform;
  }

  RealizeInstancesOptions options;
  options.keep_original_ids = false;
  options.realize_instance_attributes = false;
  bke::GeometrySet realized =
      realize_instances(bke::GeometrySet::from_instances(std::move(instances)), options).geometry;
  Mesh *result = realized.get_component_for_write<bke::MeshComponent>().release();
  if (result == nullptr) {
    return nullptr;
  }

  if (params.merge && count > 1) {
    if (std::optional<Mesh *> merged = mesh_merge_by_distance_all(
            *result, IndexMask(result->verts_num), params.merge_distance))
    {
      BKE_id_free(nullptr, result);
      result = *merged;
    }
  }
  return result;
}

}  // namespace blender::geometry

/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>
#include <array>
#include <cmath>

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_atomic_disjoint_set.hh"
#include "BLI_bounds.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_task.hh"
#include "BLI_vector.hh"

#include "BKE_attribute.hh"
#include "BKE_attribute_filter.hh"
#include "BKE_customdata.hh"
#include "BKE_geometry_set.hh"
#include "BKE_mesh.hh"

#include "GEO_join_geometries.hh"
#include "GEO_mesh_clip.hh"
#include "GEO_mesh_dice.hh"

#include "bmesh.hh"

namespace blender::geometry {

/** Name of the temporary attribute with the cell of each point. */
static constexpr const char *cell_attribute_name = ".dice_cell";

struct Piece {
  Mesh *mesh;
  int cell;
};

struct DiceData {
  const Mesh &mesh;
  /** The positions of the cell borders on each axis. */
  std::array<Array<float>, 3> borders;
  float4x4 mesh_to_space;
  float4x4 space_to_mesh;
  float gap;
  bool fill;
  float threshold;
  const std::optional<std::string> &interior_id;
};

static int cell_index(const DiceData &data, const int3 &cell)
{
  const int size_x = data.borders[0].size() - 1;
  const int size_y = data.borders[1].size() - 1;
  return cell.x + cell.y * size_x + cell.z * size_x * size_y;
}

static void cut_axis(const DiceData &data,
                     BMesh *bm,
                     const int axis,
                     const float position,
                     const BMeshClipKeep keep)
{
  float3 plane_co(0.0f);
  float3 plane_no(0.0f);
  plane_co[axis] = position;
  plane_no[axis] = 1.0f;

  BMeshClipParams params;
  params.plane_co = math::transform_point(data.space_to_mesh, plane_co);
  /* The transpose keeps the plane correct on objects with a non-uniform scale. */
  params.plane_no = math::normalize(math::transpose(float3x3(data.mesh_to_space)) * plane_no);
  params.threshold = data.threshold;
  params.keep = keep;
  params.fill = data.fill;
  if (data.interior_id) {
    params.interior_face_offset = CustomData_get_offset_named(
        &bm->pdata, CD_PROP_BOOL, data.interior_id->c_str());
  }
  bmesh_clip(bm, params);
}

/**
 * Split the mesh in half until only one cell is left, instead of cutting out each cell from the
 * whole mesh, so that the time doesn't grow with the number of cells times the size of the mesh.
 * Takes ownership of \a bm.
 */
static void dice_recursive(const DiceData &data,
                           BMesh *bm,
                           const int3 &start,
                           const int3 &end,
                           Vector<Piece> &r_pieces)
{
  if (bm->totvert == 0) {
    BM_mesh_free(bm);
    return;
  }
  const int3 sizes = end - start;
  int axis = 0;
  for (const int i : {1, 2}) {
    if (sizes[i] > sizes[axis]) {
      axis = i;
    }
  }

  if (sizes[axis] == 1) {
    Mesh *mesh = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &data.mesh);
    BM_mesh_free(bm);
    const int cell = cell_index(data, start);
    mesh->attributes_for_write().add<int>(
        cell_attribute_name,
        bke::AttrDomain::Point,
        bke::AttributeInitVArray(VArray<int>::from_single(cell, mesh->verts_num)));
    r_pieces.append({mesh, cell});
    return;
  }

  const int middle = start[axis] + sizes[axis] / 2;
  const float position = data.borders[axis][middle];
  BMesh *bm_above = BM_mesh_copy(bm);
  cut_axis(data, bm, axis, position - data.gap * 0.5f, BMeshClipKeep::Below);
  cut_axis(data, bm_above, axis, position + data.gap * 0.5f, BMeshClipKeep::Above);

  int3 end_below = end;
  end_below[axis] = middle;
  int3 start_above = start;
  start_above[axis] = middle;

  Vector<Piece> pieces_below;
  Vector<Piece> pieces_above;
  threading::parallel_invoke(
      data.mesh.verts_num > 1024,
      [&]() { dice_recursive(data, bm, start, end_below, pieces_below); },
      [&]() { dice_recursive(data, bm_above, start_above, end, pieces_above); });
  r_pieces.extend(pieces_below);
  r_pieces.extend(pieces_above);
}

static Array<float> calc_borders(const MeshDiceParams &params,
                                 const Bounds<float3> &bounds,
                                 const float threshold,
                                 const int axis)
{
  const float min = bounds.min[axis];
  const float max = bounds.max[axis];
  const float extent = max - min;
  int count = 1;
  if (extent > threshold) {
    if (params.mode == MeshDiceMode::Count) {
      count = std::max(params.counts[axis], 1);
    }
    else if (params.size[axis] > 0.0f) {
      /* Don't make a very thin last cell because of the limited precision of the division. */
      const double cells = double(extent) / double(params.size[axis]);
      count = int(std::min(std::ceil(cells - 1e-4), double(mesh_dice_max_cells + 1)));
      count = std::max(count, 1);
    }
  }
  if (count > mesh_dice_max_cells) {
    return Array<float>(int(mesh_dice_max_cells + 2), 0.0f);
  }
  Array<float> borders(count + 1);
  for (const int i : borders.index_range()) {
    borders[i] = params.mode == MeshDiceMode::Count ?
                     min + extent * (float(i) / float(count)) :
                     std::min(min + params.size[axis] * float(i), max);
  }
  borders.last() = max;
  return borders;
}

/** Number the pieces from zero, or number the connected parts if they should be separate. */
static void add_piece_attributes(Mesh &mesh, const Span<Piece> pieces, const MeshDiceParams &params)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  const VArraySpan<int> cells = *attributes.lookup<int>(cell_attribute_name, bke::AttrDomain::Point);

  if (params.cell_index_id) {
    attributes.add<int>(*params.cell_index_id,
                        bke::AttrDomain::Point,
                        bke::AttributeInitVArray(VArray<int>::from_span(cells)));
  }
  if (params.piece_index_id) {
    Array<int> piece_indices(mesh.verts_num);
    if (params.separate_islands) {
      AtomicDisjointSet islands(mesh.verts_num);
      threading::parallel_for(mesh.edges().index_range(), 4096, [&](const IndexRange range) {
        for (const int2 &edge : mesh.edges().slice(range)) {
          islands.join(edge[0], edge[1]);
        }
      });
      islands.calc_reduced_ids(piece_indices);
    }
    else {
      /* The pieces are sorted by their cell. */
      Array<int> sorted_cells(pieces.size());
      for (const int i : pieces.index_range()) {
        sorted_cells[i] = pieces[i].cell;
      }
      threading::parallel_for(piece_indices.index_range(), 4096, [&](const IndexRange range) {
        for (const int i : range) {
          piece_indices[i] = std::lower_bound(sorted_cells.begin(), sorted_cells.end(), cells[i]) -
                             sorted_cells.begin();
        }
      });
    }
    attributes.add<int>(*params.piece_index_id,
                        bke::AttrDomain::Point,
                        bke::AttributeInitVArray(VArray<int>::from_span(piece_indices)));
  }
}

Mesh *mesh_dice(const Mesh &mesh, const MeshDiceParams &params)
{
  const float4x4 &mesh_to_space = params.mesh_to_space;
  const float4x4 space_to_mesh = math::invert(mesh_to_space);

  Array<float3> space_positions(mesh.vert_positions());
  math::transform_points(mesh_to_space, space_positions);
  const Bounds<float3> bounds = *bounds::min_max(space_positions.as_span());
  const float threshold = std::max(math::distance(bounds.min, bounds.max) * 1e-5f, 1e-7f);

  std::array<Array<float>, 3> borders;
  int64_t total_cells = 1;
  for (const int axis : IndexRange(3)) {
    borders[axis] = calc_borders(params, bounds, threshold, axis);
    total_cells *= borders[axis].size() - 1;
    if (total_cells > mesh_dice_max_cells) {
      return nullptr;
    }
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);
  if (params.interior_id) {
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, params.interior_id->c_str());
  }

  const int3 end(borders[0].size() - 1, borders[1].size() - 1, borders[2].size() - 1);
  DiceData data{mesh,
                std::move(borders),
                mesh_to_space,
                space_to_mesh,
                std::max(params.gap, 0.0f),
                params.fill,
                threshold,
                params.interior_id};
  Vector<Piece> pieces;
  dice_recursive(data, bm, int3(0), end, pieces);

  if (pieces.is_empty()) {
    return BKE_mesh_new_nomain_from_template(&mesh, 0, 0, 0, 0);
  }
  std::sort(pieces.begin(), pieces.end(), [](const Piece &a, const Piece &b) {
    return a.cell < b.cell;
  });

  Array<bke::GeometrySet> piece_geometries(pieces.size());
  for (const int i : pieces.index_range()) {
    piece_geometries[i] = bke::GeometrySet::from_mesh(pieces[i].mesh);
  }
  bke::GeometrySet joined = join_geometries(piece_geometries,
                                            bke::AttributeFilter::default_filter());
  piece_geometries = {};

  Mesh *result = joined.get_component_for_write<bke::MeshComponent>().release();
  add_piece_attributes(*result, pieces, params);
  result->attributes_for_write().remove(cell_attribute_name);
  return result;
}

}  // namespace blender::geometry

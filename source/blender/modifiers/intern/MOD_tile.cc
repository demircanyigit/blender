/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Tile: wraps the mesh into one tile so it repeats without seams. Parts that stick out past a
 * border are cut off there and moved to the opposite side, so copies of the result placed side by
 * side line up.
 */

#include <algorithm>
#include <cmath>

#include "BLI_array.hh"
#include "BLI_bounds.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_geom.h"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"

#include "BLT_translation.hh"

#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_geometry_set.hh"
#include "BKE_instances.hh"
#include "BKE_lib_id.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "bmesh.hh"
#include "bmesh_tools.hh"

#include "GEO_mesh_merge_by_distance.hh"
#include "GEO_mesh_split_edges.hh"
#include "GEO_realize_instances.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

/** More cuts than this per axis means the tile is far too small for the mesh. */
static constexpr int MAX_CUTS = 1000;

static void init_data(ModifierData *md)
{
  TileModifierData *data = reinterpret_cast<TileModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  TileModifierData *tmd = reinterpret_cast<TileModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&tmd->center), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  TileModifierData *tmd = reinterpret_cast<TileModifierData *>(md);
  if (tmd->center != nullptr) {
    DEG_add_object_relation(ctx->node, tmd->center, DEG_OB_COMP_TRANSFORM, "Tile Modifier");
    DEG_add_depends_on_transform_relation(ctx->node, "Tile Modifier");
  }
}

/** Cuts the mesh at every tile border that crosses it. */
static Mesh *cut_at_borders(const Mesh &mesh,
                            const Bounds<float3> &bounds,
                            const bool use_axis[2],
                            const float2 lo,
                            const float2 size,
                            const float eps)
{
  BMeshCreateParams create_params{};
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);
  for (const int axis : IndexRange(2)) {
    if (!use_axis[axis]) {
      continue;
    }
    const int first = int(std::floor((bounds.min[axis] - lo[axis]) / size[axis])) + 1;
    for (int k = first;; k++) {
      const float border = lo[axis] + float(k) * size[axis];
      if (border >= bounds.max[axis] - eps) {
        break;
      }
      if (border <= bounds.min[axis] + eps) {
        continue;
      }
      float3 co(0.0f);
      co[axis] = border;
      float3 no(0.0f);
      no[axis] = 1.0f;
      float plane[4];
      plane_from_point_normal_v3(plane, co, no);
      BM_mesh_bisect_plane(bm, plane, false, false, 0, 0, eps);
    }
  }
  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

static Mesh *modify_mesh(ModifierData *md, const ModifierEvalContext *ctx, Mesh *mesh)
{
  const TileModifierData &tmd = *reinterpret_cast<const TileModifierData *>(md);
  const std::optional<Bounds<float3>> bounds = mesh->bounds_min_max();
  if (!bounds) {
    return mesh;
  }
  const bool use_axis[2] = {tmd.axes != MOD_TILE_AXES_Y, tmd.axes != MOD_TILE_AXES_X};
  const float2 size = math::max(float2(tmd.size), float2(1e-4f));
  float2 center(0.0f);
  if (tmd.center != nullptr) {
    center = math::transform_point(ctx->object->world_to_object(),
                                   float3(tmd.center->object_to_world().location()))
                 .xy();
  }
  const float2 lo = center - size * 0.5f;
  const float eps = std::max(math::distance(bounds->min, bounds->max) * 1e-6f, 1e-6f);

  for (const int axis : IndexRange(2)) {
    if (use_axis[axis] && (bounds->max[axis] - bounds->min[axis]) / size[axis] > MAX_CUTS) {
      BKE_modifier_set_warning(ctx->object, md, "The tile is too small for the mesh");
      return mesh;
    }
  }

  /* Which tile a point is in, counted from the middle one. */
  auto cell_of = [&](const float3 &p) {
    int2 cell(0);
    for (const int axis : IndexRange(2)) {
      if (use_axis[axis]) {
        cell[axis] = int(std::floor((p[axis] - lo[axis]) / size[axis]));
      }
    }
    return cell;
  };

  bool crosses = false;
  for (const int axis : IndexRange(2)) {
    if (use_axis[axis] && (cell_of(bounds->min)[axis] != 0 || cell_of(bounds->max)[axis] != 0)) {
      crosses = true;
    }
  }

  Mesh *result = crosses ? cut_at_borders(*mesh, *bounds, use_axis, lo, size, eps) :
                           BKE_mesh_copy_for_eval(*mesh);

  if (crosses) {
    /* Each face is now inside one tile. Faces in different tiles get their own points, so
     * they can move apart. */
    auto face_cells = [&](const Mesh &m) {
      const Span<float3> positions = m.vert_positions();
      const OffsetIndices faces = m.faces();
      const Span<int> corner_verts = m.corner_verts();
      Array<int2> cells(faces.size());
      for (const int f : faces.index_range()) {
        float3 mid(0.0f);
        for (const int v : corner_verts.slice(faces[f])) {
          mid += positions[v];
        }
        cells[f] = cell_of(mid / float(faces[f].size()));
      }
      return cells;
    };

    {
      const Array<int2> cells = face_cells(*result);
      const OffsetIndices faces = result->faces();
      const Span<int> corner_edges = result->corner_edges();
      Array<int2> edge_cell(result->edges_num);
      Array<bool> edge_seen(result->edges_num, false);
      Array<bool> split(result->edges_num, false);
      bool any_split = false;
      for (const int f : faces.index_range()) {
        for (const int e : corner_edges.slice(faces[f])) {
          if (!edge_seen[e]) {
            edge_seen[e] = true;
            edge_cell[e] = cells[f];
          }
          else if (edge_cell[e] != cells[f]) {
            split[e] = true;
            any_split = true;
          }
        }
      }
      if (any_split) {
        IndexMaskMemory memory;
        geometry::split_edges(*result, IndexMask::from_bools(split, memory));
      }
    }

    /* Move every point back into the middle tile, with the face or edge it belongs to. */
    const Array<int2> cells = face_cells(*result);
    const OffsetIndices faces = result->faces();
    const Span<int> corner_verts = result->corner_verts();
    const Span<int2> edges = result->edges();
    MutableSpan<float3> positions = result->vert_positions_for_write();
    Array<int2> vert_cell(result->verts_num);
    Array<bool> vert_set(result->verts_num, false);
    for (const int f : faces.index_range()) {
      for (const int v : corner_verts.slice(faces[f])) {
        if (!vert_set[v]) {
          vert_set[v] = true;
          vert_cell[v] = cells[f];
        }
      }
    }
    for (const int2 &edge : edges) {
      if (!vert_set[edge[0]] && !vert_set[edge[1]]) {
        const int2 cell = cell_of(math::midpoint(positions[edge[0]], positions[edge[1]]));
        for (const int v : {edge[0], edge[1]}) {
          vert_set[v] = true;
          vert_cell[v] = cell;
        }
      }
    }
    for (const int v : positions.index_range()) {
      const int2 cell = vert_set[v] ? vert_cell[v] : cell_of(positions[v]);
      positions[v].x -= float(cell.x) * size.x;
      positions[v].y -= float(cell.y) * size.y;
    }
    result->tag_positions_changed();

    /* Pieces moved in from both sides meet on the borders: join them there. */
    if (tmd.flag & MOD_TILE_MERGE) {
      const float dist = std::max(tmd.merge_distance, 1e-7f);
      const float2 hi = lo + size;
      Array<bool> on_border(result->verts_num, false);
      for (const int v : positions.index_range()) {
        for (const int axis : IndexRange(2)) {
          if (use_axis[axis] && (std::abs(positions[v][axis] - lo[axis]) <= dist ||
                                 std::abs(positions[v][axis] - hi[axis]) <= dist))
          {
            on_border[v] = true;
          }
        }
      }
      IndexMaskMemory memory;
      const IndexMask selection = IndexMask::from_bools(on_border, memory);
      if (!selection.is_empty()) {
        if (std::optional<Mesh *> merged = geometry::mesh_merge_by_distance_all(
                *result, selection, dist))
        {
          BKE_id_free(nullptr, result);
          result = *merged;
        }
      }
    }
  }

  /* The neighboring tiles, only to look at while modeling. */
  const bool preview = (tmd.flag & MOD_TILE_PREVIEW) &&
                       !(ctx->flag & (MOD_APPLY_RENDER | MOD_APPLY_TO_ORIGINAL));
  if (preview) {
    Vector<float4x4> transforms;
    for (int y = -1; y <= 1; y++) {
      for (int x = -1; x <= 1; x++) {
        if ((x != 0 && !use_axis[0]) || (y != 0 && !use_axis[1])) {
          continue;
        }
        transforms.append(
            math::from_location<float4x4>(float3(float(x) * size.x, float(y) * size.y, 0.0f)));
      }
    }
    std::unique_ptr<bke::Instances> instances = std::make_unique<bke::Instances>(
        transforms.size());
    /* The instance owns the mesh from here on. */
    const int handle = instances->add_reference(
        bke::InstanceReference(bke::GeometrySet::from_mesh(result)));
    result = nullptr;
    instances->reference_handles_for_write().fill(handle);
    instances->transforms_for_write().copy_from(transforms);
    bke::GeometrySet tiles = bke::GeometrySet::from_instances(instances.release());
    tiles = geometry::realize_instances(tiles, geometry::RealizeInstancesOptions()).geometry;
    result = tiles.has_mesh() ? tiles.get_component_for_write<bke::MeshComponent>().release() :
                                BKE_mesh_new_nomain(0, 0, 0, 0);
  }
  return result;
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "size", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "axes", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.prop(ptr, "center", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "show_preview", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  ui::Layout &row = layout.row(true, IFACE_("Merge"));
  row.prop(ptr, "use_merge", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &sub = row.row(true);
  sub.active_set(RNA_boolean_get(ptr, "use_merge"));
  sub.prop(ptr, "merge_distance", UI_ITEM_NONE, "", ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Tile, panel_draw);
}

ModifierTypeInfo modifierType_Tile = {
    /*idname*/ "Tile",
    /*name*/ N_("Tile"),
    /*struct_name*/ "TileModifierData",
    /*struct_size*/ sizeof(TileModifierData),
    /*srna*/ &RNA_TileModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_TILE,
    /*copy_data*/ BKE_modifier_copydata_generic,
    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ modify_mesh,
    /*modify_geometry_set*/ nullptr,
    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ nullptr,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender

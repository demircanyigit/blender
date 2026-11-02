/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"

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
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_join_geometries.hh"
#include "GEO_mesh_clip.hh"
#include "GEO_randomize.hh"

#include "bmesh.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_dice_mesh_cc {

enum class Mode {
  Count = 0,
  Size = 1,
};

enum class Space {
  Object = 0,
  World = 1,
};

static const EnumPropertyItem mode_items[] = {
    {int(Mode::Count), "COUNT", ICON_NONE, N_("Count"), N_("Choose the number of cells on each axis")},
    {int(Mode::Size),
     "SIZE",
     ICON_NONE,
     N_("Size"),
     N_("Choose the size of the cells. The last cell on each axis is smaller if the bounding box "
        "doesn't divide evenly")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem space_items[] = {
    {int(Space::Object),
     "OBJECT",
     ICON_NONE,
     N_("Object"),
     N_("Use the bounding box and axes of the object, so the cuts rotate with it")},
    {int(Space::World),
     "WORLD",
     ICON_NONE,
     N_("World"),
     N_("Use the bounding box and axes of the world, so the cuts stay straight when the object "
        "rotates")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to cut into pieces");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Int>("Piece Index")
      .field_on_all()
      .description("Number of the piece, which can be used to split the mesh into the pieces");
  b.add_output<decl::Int>("Cell Index")
      .field_on_all()
      .description("Position of the piece in the grid: X + Y * Count X + Z * Count X * Count Y");
  b.add_output<decl::Bool>("Interior")
      .field_on_all()
      .description("The faces that fill the cuts");

  b.add_input<decl::Menu>("Mode")
      .static_items(mode_items)
      .default_value(Mode::Count)
      .optional_label();
  b.add_input<decl::Menu>("Space")
      .static_items(space_items)
      .default_value(Space::Object)
      .description("Which bounding box and axes to cut along");
  b.add_input<decl::Int>("Count X")
      .default_value(2)
      .min(1)
      .usage_by_menu("Mode", int(Mode::Count))
      .description("Number of cells along the X axis of the bounding box");
  b.add_input<decl::Int>("Count Y")
      .default_value(2)
      .min(1)
      .usage_by_menu("Mode", int(Mode::Count))
      .description("Number of cells along the Y axis of the bounding box");
  b.add_input<decl::Int>("Count Z")
      .default_value(2)
      .min(1)
      .usage_by_menu("Mode", int(Mode::Count))
      .description("Number of cells along the Z axis of the bounding box");
  b.add_input<decl::Vector>("Size")
      .default_value({0.5f, 0.5f, 0.5f})
      .min(0.0f)
      .subtype(PROP_TRANSLATION)
      .usage_by_menu("Mode", int(Mode::Size))
      .description("Size of the cells on each axis. Zero doesn't cut along that axis");
  b.add_input<decl::Float>("Cut Gap")
      .default_value(0.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance between neighboring pieces");
  b.add_input<decl::Bool>("Fill Cuts")
      .default_value(true)
      .description("Close the holes left by the cuts, so that closed meshes make solid pieces");
  b.add_input<decl::Bool>("Separate Islands")
      .default_value(false)
      .description(
          "Make a separate piece of every part of a cell that isn't connected to the rest of it");
}

/** Pieces with more cells than this are probably a mistake, and would take very long. */
static constexpr int64_t max_cells = 1000000;

/** Name of the temporary attribute with the cell of each point. */
static constexpr const char *cell_attribute_name = ".dice_node_cell";

struct Settings {
  Mode mode;
  Space space;
  int3 counts;
  float3 size;
  float gap;
  bool fill;
  bool separate_islands;
  std::optional<std::string> piece_index_id;
  std::optional<std::string> cell_index_id;
  std::optional<std::string> interior_id;
};

struct Piece {
  Mesh *mesh;
  int cell;
};

struct DiceData {
  const Mesh &mesh;
  /** The positions of the cell borders on each axis. */
  std::array<Array<float>, 3> borders;
  float4x4 object_to_space;
  float4x4 space_to_object;
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
                     const geometry::BMeshClipKeep keep)
{
  float3 plane_co(0.0f);
  float3 plane_no(0.0f);
  plane_co[axis] = position;
  plane_no[axis] = 1.0f;

  geometry::BMeshClipParams params;
  params.plane_co = math::transform_point(data.space_to_object, plane_co);
  /* The transpose keeps the plane correct on objects with a non-uniform scale. */
  params.plane_no = math::normalize(math::transpose(float3x3(data.object_to_space)) * plane_no);
  params.threshold = data.threshold;
  params.keep = keep;
  params.fill = data.fill;
  if (data.interior_id) {
    params.interior_face_offset = CustomData_get_offset_named(
        &bm->pdata, CD_PROP_BOOL, data.interior_id->c_str());
  }
  geometry::bmesh_clip(bm, params);
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
  cut_axis(data, bm, axis, position - data.gap * 0.5f, geometry::BMeshClipKeep::Below);
  cut_axis(data, bm_above, axis, position + data.gap * 0.5f, geometry::BMeshClipKeep::Above);

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

static Array<float> calc_borders(const Settings &settings,
                                 const Bounds<float3> &bounds,
                                 const float threshold,
                                 const int axis)
{
  const float min = bounds.min[axis];
  const float max = bounds.max[axis];
  const float extent = max - min;
  int count = 1;
  if (extent > threshold) {
    if (settings.mode == Mode::Count) {
      count = std::max(settings.counts[axis], 1);
    }
    else if (settings.size[axis] > 0.0f) {
      /* Don't make a very thin last cell because of the limited precision of the division. */
      const double cells = double(extent) / double(settings.size[axis]);
      count = int(std::min(std::ceil(cells - 1e-4), double(max_cells + 1)));
      count = std::max(count, 1);
    }
  }
  if (count > max_cells) {
    return Array<float>(int(max_cells + 2), 0.0f);
  }
  Array<float> borders(count + 1);
  for (const int i : borders.index_range()) {
    borders[i] = settings.mode == Mode::Count ?
                     min + extent * (float(i) / float(count)) :
                     std::min(min + settings.size[axis] * float(i), max);
  }
  borders.last() = max;
  return borders;
}

/** Number the pieces from zero, or number the connected parts if they should be separate. */
static void add_piece_attributes(Mesh &mesh, const Span<Piece> pieces, const Settings &settings)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  const VArraySpan<int> cells = *attributes.lookup<int>(cell_attribute_name, bke::AttrDomain::Point);

  if (settings.cell_index_id) {
    attributes.add<int>(*settings.cell_index_id,
                        bke::AttrDomain::Point,
                        bke::AttributeInitVArray(VArray<int>::from_span(cells)));
  }
  if (settings.piece_index_id) {
    Array<int> piece_indices(mesh.verts_num);
    if (settings.separate_islands) {
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
    attributes.add<int>(*settings.piece_index_id,
                        bke::AttrDomain::Point,
                        bke::AttributeInitVArray(VArray<int>::from_span(piece_indices)));
  }
}

/** \return The new mesh, or null if there would be too many cells. */
static Mesh *dice_mesh(const Mesh &mesh, const Object *self_object, const Settings &settings)
{
  const bool use_world = settings.space == Space::World && self_object != nullptr;
  const float4x4 object_to_space = use_world ? self_object->object_to_world() : float4x4::identity();
  const float4x4 space_to_object = use_world ? self_object->world_to_object() : float4x4::identity();

  Array<float3> space_positions(mesh.vert_positions());
  if (use_world) {
    math::transform_points(object_to_space, space_positions);
  }
  const Bounds<float3> bounds = *bounds::min_max(space_positions.as_span());
  const float threshold = std::max(math::distance(bounds.min, bounds.max) * 1e-5f, 1e-7f);

  std::array<Array<float>, 3> borders;
  int64_t total_cells = 1;
  for (const int axis : IndexRange(3)) {
    borders[axis] = calc_borders(settings, bounds, threshold, axis);
    total_cells *= borders[axis].size() - 1;
    if (total_cells > max_cells) {
      return nullptr;
    }
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);
  if (settings.interior_id) {
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, settings.interior_id->c_str());
  }

  const int3 end(borders[0].size() - 1, borders[1].size() - 1, borders[2].size() - 1);
  DiceData data{mesh,
                std::move(borders),
                object_to_space,
                space_to_object,
                settings.gap,
                settings.fill,
                threshold,
                settings.interior_id};
  Vector<Piece> pieces;
  dice_recursive(data, bm, int3(0), end, pieces);

  if (pieces.is_empty()) {
    return BKE_mesh_new_nomain_from_template(&mesh, 0, 0, 0, 0);
  }
  std::sort(pieces.begin(), pieces.end(), [](const Piece &a, const Piece &b) {
    return a.cell < b.cell;
  });

  Array<GeometrySet> piece_geometries(pieces.size());
  for (const int i : pieces.index_range()) {
    piece_geometries[i] = GeometrySet::from_mesh(pieces[i].mesh);
  }
  GeometrySet joined = geometry::join_geometries(piece_geometries,
                                                 bke::AttributeFilter::default_filter());
  piece_geometries = {};

  Mesh *result = joined.get_component_for_write<bke::MeshComponent>().release();
  add_piece_attributes(*result, pieces, settings);
  result->attributes_for_write().remove(cell_attribute_name);
  return result;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");

  Settings settings;
  settings.mode = params.extract_input<Mode>("Mode");
  settings.space = params.extract_input<Space>("Space");
  settings.counts = int3(params.extract_input<int>("Count X"),
                         params.extract_input<int>("Count Y"),
                         params.extract_input<int>("Count Z"));
  settings.size = params.extract_input<float3>("Size");
  settings.gap = std::max(params.extract_input<float>("Cut Gap"), 0.0f);
  settings.fill = params.extract_input<bool>("Fill Cuts");
  settings.separate_islands = params.extract_input<bool>("Separate Islands");
  settings.piece_index_id = params.get_output_anonymous_attribute_id_if_needed("Piece Index");
  settings.cell_index_id = params.get_output_anonymous_attribute_id_if_needed("Cell Index");
  settings.interior_id = params.get_output_anonymous_attribute_id_if_needed("Interior");

  const Object *self_object = params.self_object();
  std::atomic<bool> too_many_cells = false;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    Mesh *result = dice_mesh(*src_mesh, self_object, settings);
    if (result == nullptr) {
      too_many_cells = true;
      return;
    }
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  if (too_many_cells) {
    params.error_message_add(NodeWarningType::Warning,
                             TIP_("Too many cells, the limit is one million"));
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeDiceMesh");
  ntype.ui_name = "Dice";
  ntype.ui_description =
      "Cut a mesh into a grid of pieces along the X, Y and Z axes of its bounding box";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_dice_mesh_cc

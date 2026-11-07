/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <algorithm>

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"

#include "BLI_math_matrix.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_dice.hh"
#include "GEO_randomize.hh"

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

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");

  geometry::MeshDiceParams settings;
  settings.mode = geometry::MeshDiceMode(params.extract_input<Mode>("Mode"));
  const Space space = params.extract_input<Space>("Space");
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
  if (space == Space::World && self_object != nullptr) {
    settings.mesh_to_space = self_object->object_to_world();
  }
  std::atomic<bool> too_many_cells = false;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    Mesh *result = geometry::mesh_dice(*src_mesh, settings);
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

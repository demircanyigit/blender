/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BLI_array.hh"
#include "BLI_math_vector.hh"

#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_displace_mesh_cc {

enum class Mode {
  Normal = 0,
  Direction = 1,
  Vector = 2,
};

static const EnumPropertyItem mode_items[] = {
    {int(Mode::Normal),
     "NORMAL",
     ICON_NONE,
     N_("Normal"),
     N_("Move points along their normals by the height")},
    {int(Mode::Direction),
     "DIRECTION",
     ICON_NONE,
     N_("Direction"),
     N_("Move points along a direction by the height")},
    {int(Mode::Vector),
     "VECTOR",
     ICON_NONE,
     N_("Vector"),
     N_("Move points by a vector, for example from the color of a texture")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to displace");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Menu>("Mode")
      .static_items(mode_items)
      .default_value(Mode::Normal)
      .optional_label();
  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Points to move");
  b.add_input<decl::Float>("Height")
      .default_value(1.0f)
      .field_on_all()
      .usage_by_menu("Mode", {int(Mode::Normal), int(Mode::Direction)})
      .description("Height of each point, for example from a texture");
  b.add_input<decl::Vector>("Direction")
      .default_value({0.0f, 0.0f, 1.0f})
      .subtype(PROP_XYZ)
      .field_on_all()
      .usage_by_menu("Mode", int(Mode::Direction))
      .description("Direction to move each point in. Its length doesn't matter");
  b.add_input<decl::Vector>("Vector")
      .default_value({1.0f, 1.0f, 1.0f})
      .subtype(PROP_XYZ)
      .field_on_all()
      .usage_by_menu("Mode", int(Mode::Vector))
      .description("Vector to move each point by, for example from the color of a texture");
  b.add_input<decl::Float>("Midlevel")
      .default_value(0.5f)
      .description(
          "The value that doesn't move a point. Lower values go backward, and in Vector mode this "
          "is subtracted from every axis");
  b.add_input<decl::Float>("Strength")
      .default_value(1.0f)
      .field_on_all()
      .description("Multiplier of the movement of each point");
}

static void displace_mesh(Mesh &mesh,
                          const Mode mode,
                          const float midlevel,
                          const Field<bool> &selection_field,
                          const Field<float> &height_field,
                          const Field<float3> &direction_field,
                          const Field<float3> &vector_field,
                          const Field<float> &strength_field)
{
  const bke::MeshFieldContext context(mesh, bke::AttrDomain::Point);
  FieldEvaluator evaluator(context, mesh.verts_num);
  evaluator.set_selection(selection_field);
  evaluator.add(height_field);
  evaluator.add(direction_field);
  evaluator.add(vector_field);
  evaluator.add(strength_field);
  evaluator.evaluate();
  const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
  if (selection.is_empty()) {
    return;
  }
  const VArray<float> heights = evaluator.get_evaluated<float>(0);
  const VArray<float3> directions = evaluator.get_evaluated<float3>(1);
  const VArray<float3> vectors = evaluator.get_evaluated<float3>(2);
  const VArray<float> strengths = evaluator.get_evaluated<float>(3);

  /* Smooth normals, so that flat shaded faces don't come apart. */
  Array<float3> normals;
  if (mode == Mode::Normal) {
    normals = mesh.vert_normals();
  }

  /* The positions can be used by the fields, so they are only changed once those are done. */
  Array<float3> offsets(mesh.verts_num);
  selection.foreach_index(GrainSize(2048), [&](const int i) {
    switch (mode) {
      case Mode::Normal:
        offsets[i] = normals[i] * ((heights[i] - midlevel) * strengths[i]);
        break;
      case Mode::Direction:
        offsets[i] = math::normalize(directions[i]) * ((heights[i] - midlevel) * strengths[i]);
        break;
      case Mode::Vector:
        offsets[i] = (vectors[i] - float3(midlevel)) * strengths[i];
        break;
    }
  });

  MutableSpan<float3> positions = mesh.vert_positions_for_write();
  selection.foreach_index(GrainSize(4096), [&](const int i) { positions[i] += offsets[i]; });
  mesh.tag_positions_changed();
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Mode mode = params.extract_input<Mode>("Mode");
  const float midlevel = params.extract_input<float>("Midlevel");
  const Field<bool> selection = params.extract_input<Field<bool>>("Selection");
  const Field<float> height = params.extract_input<Field<float>>("Height");
  const Field<float3> direction = params.extract_input<Field<float3>>("Direction");
  const Field<float3> vector = params.extract_input<Field<float3>>("Vector");
  const Field<float> strength = params.extract_input<Field<float>>("Strength");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    Mesh *mesh = geometry_set.get_mesh_for_write();
    if (mesh == nullptr || mesh->verts_num == 0) {
      return;
    }
    displace_mesh(*mesh, mode, midlevel, selection, height, direction, vector, strength);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeDisplaceMesh");
  ntype.ui_name = "Displace";
  ntype.ui_description = "Move the points of a mesh by a height or vector, like the Displace modifier";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_displace_mesh_cc

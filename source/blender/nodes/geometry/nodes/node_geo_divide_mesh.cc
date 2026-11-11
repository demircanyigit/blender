/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <mutex>

#include "DNA_mesh_types.h"

#include "BLI_math_euler.hh"
#include "BLI_math_rotation.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_divide.hh"
#include "GEO_randomize.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_divide_mesh_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to cut");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Vector>("Size")
      .default_value(float3(0.5f))
      .min(0.0001f)
      .subtype(PROP_TRANSLATION)
      .description("Distance between the cuts along each axis");
  b.add_input<decl::Vector>("Offset")
      .subtype(PROP_TRANSLATION)
      .description("Moves the grid of cuts");
  b.add_input<decl::Rotation>("Rotation").description("Turns the grid of cuts around the offset");
  b.add_input<decl::Bool>("X").default_value(true).description("Cut across the X axis");
  b.add_input<decl::Bool>("Y").default_value(true).description("Cut across the Y axis");
  b.add_input<decl::Bool>("Z").default_value(true).description("Cut across the Z axis");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");

  geometry::MeshDivideParams settings;
  settings.size = math::max(params.extract_input<float3>("Size"), float3(0.0001f));
  settings.offset = params.extract_input<float3>("Offset");
  const math::EulerXYZ euler = math::to_euler(params.extract_input<math::Quaternion>("Rotation"));
  settings.rotation = float3(euler.x().radian(), euler.y().radian(), euler.z().radian());
  settings.axes = {params.extract_input<bool>("X"),
                   params.extract_input<bool>("Y"),
                   params.extract_input<bool>("Z")};

  std::mutex message_mutex;
  std::string message;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->faces_num == 0) {
      return;
    }
    std::string mesh_message;
    Mesh *result = geometry::mesh_divide(*src_mesh, settings, mesh_message);
    if (!mesh_message.empty()) {
      std::lock_guard lock{message_mutex};
      message = mesh_message;
    }
    if (result == nullptr) {
      return;
    }
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  if (!message.empty()) {
    params.error_message_add(NodeWarningType::Warning, message);
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeDivideMesh");
  ntype.ui_name = "Divide Mesh";
  ntype.ui_description = "Cut the faces of a mesh with a grid, keeping it connected";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_divide_mesh_cc

/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_smooth.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_relax_mesh_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to relax");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Points that may move");
  b.add_input<decl::Float>("Factor")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .field_on_all()
      .description("How far each point moves in one iteration");
  b.add_input<decl::Int>("Iterations")
      .default_value(10)
      .min(0)
      .max(10000)
      .description("Number of relax passes");
  b.add_input<decl::Bool>("Keep on Surface")
      .default_value(true)
      .description("Move the points back onto the original surface after every iteration");
  b.add_input<decl::Bool>("Preserve Boundary")
      .default_value(true)
      .description("Keep the points on open boundaries in place");
  b.add_input<decl::Bool>("Hard Edges")
      .default_value(false)
      .hide_value()
      .field_on_all()
      .description("Edges whose points stay in place");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  const Field<float> factor_field = params.extract_input<Field<float>>("Factor");
  const Field<bool> hard_edges_field = params.extract_input<Field<bool>>("Hard Edges");
  const bool keep_on_surface = params.extract_input<bool>("Keep on Surface");

  geometry::MeshSmoothParams smooth_params;
  smooth_params.iterations = std::clamp(params.extract_input<int>("Iterations"), 0, 10000);
  smooth_params.preserve_boundary = params.extract_input<bool>("Preserve Boundary");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    Mesh *mesh = geometry_set.get_mesh_for_write();
    if (mesh == nullptr || mesh->verts_num == 0) {
      return;
    }
    const bke::MeshFieldContext point_context(*mesh, bke::AttrDomain::Point);
    FieldEvaluator point_evaluator(point_context, mesh->verts_num);
    point_evaluator.set_selection(selection_field);
    point_evaluator.add(factor_field);
    point_evaluator.evaluate();

    const bke::MeshFieldContext edge_context(*mesh, bke::AttrDomain::Edge);
    FieldEvaluator edge_evaluator(edge_context, mesh->edges_num);
    edge_evaluator.add(hard_edges_field);
    edge_evaluator.evaluate();

    geometry::mesh_relax(*mesh,
                         point_evaluator.get_evaluated_selection_as_mask(),
                         point_evaluator.get_evaluated<float>(0),
                         edge_evaluator.get_evaluated<bool>(0),
                         keep_on_surface,
                         smooth_params);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeRelaxMesh");
  ntype.ui_name = "Relax";
  ntype.ui_description =
      "Even out the distances between the points of a mesh by sliding them along its surface";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_relax_mesh_cc

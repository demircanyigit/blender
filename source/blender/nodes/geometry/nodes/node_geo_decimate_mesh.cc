/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BLI_math_base.h"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_decimate.hh"
#include "GEO_randomize.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_decimate_mesh_cc {

static EnumPropertyItem mode_items[] = {
    {int(geometry::MeshDecimateMode::Ratio),
     "RATIO",
     0,
     N_("Ratio"),
     N_("Keep a fraction of the selected vertices")},
    {int(geometry::MeshDecimateMode::FaceCount),
     "FACE_COUNT",
     0,
     N_("Face Count"),
     N_("Aim for a number of triangles in the result")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to reduce the number of triangles of");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Vertices that may be removed");
  b.add_input<decl::Menu>("Mode")
      .static_items(mode_items)
      .optional_label()
      .description("How to choose the size of the result");
  b.add_input<decl::Float>("Ratio")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu("Mode", int(geometry::MeshDecimateMode::Ratio))
      .description("Fraction of the selected vertices to keep");
  b.add_input<decl::Int>("Face Count")
      .default_value(1000)
      .min(0)
      .usage_by_menu("Mode", int(geometry::MeshDecimateMode::FaceCount))
      .description("Desired number of triangles in the result");
  b.add_input<decl::Float>("Max Error")
      .default_value(0.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description(
          "Largest allowed distance to the original surface, which can stop the decimation before "
          "the target is reached. Zero disables the limit");
  b.add_input<decl::Float>("Max Normal Change")
      .default_value(0.0f)
      .min(0.0f)
      .max(float(M_PI))
      .subtype(PROP_ANGLE)
      .description(
          "Largest allowed change of the face normals, which can stop the decimation before the "
          "target is reached. Zero disables the limit");
  b.add_input<decl::Float>("Max Aspect Ratio")
      .default_value(10.0f)
      .min(0.0f)
      .description(
          "Do not make triangles more elongated than this, which can stop the decimation before "
          "the target is reached. Zero disables the limit");
  b.add_input<decl::Bool>("Preserve Boundary")
      .default_value(true)
      .description("Keep the shape of open boundaries");
  b.add_input<decl::Bool>("Hard Edges")
      .default_value(false)
      .hide_value()
      .field_on_all()
      .description(
          "Edges to keep regardless of their angle, for example marked sharp edges from the "
          "\"sharp_edge\" attribute or material boundaries");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const AttributeFilter &attribute_filter = params.get_attribute_filter("Mesh");

  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  const Field<bool> hard_edges_field = params.extract_input<Field<bool>>("Hard Edges");

  geometry::MeshDecimateParams decimate_params;
  decimate_params.mode = params.extract_input<geometry::MeshDecimateMode>("Mode");
  decimate_params.ratio = params.extract_input<float>("Ratio");
  decimate_params.face_count = params.extract_input<int>("Face Count");
  decimate_params.max_error = params.extract_input<float>("Max Error");
  decimate_params.max_normal_change = params.extract_input<float>("Max Normal Change");
  decimate_params.max_aspect_ratio = params.extract_input<float>("Max Aspect Ratio");
  decimate_params.preserve_boundary = params.extract_input<bool>("Preserve Boundary");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->faces_num == 0) {
      return;
    }

    const bke::MeshFieldContext point_context(*src_mesh, AttrDomain::Point);
    fn::FieldEvaluator point_evaluator(point_context, src_mesh->verts_num);
    point_evaluator.add(selection_field);
    point_evaluator.evaluate();

    const bke::MeshFieldContext edge_context(*src_mesh, AttrDomain::Edge);
    fn::FieldEvaluator edge_evaluator(edge_context, src_mesh->edges_num);
    edge_evaluator.add(hard_edges_field);
    edge_evaluator.evaluate();

    geometry::MeshDecimateParams mesh_params = decimate_params;
    mesh_params.selection = point_evaluator.get_evaluated<bool>(0);
    mesh_params.hard_edges = edge_evaluator.get_evaluated<bool>(0);

    int detached_tris = 0;
    Mesh *result = geometry::decimate_mesh(
        *src_mesh, mesh_params, attribute_filter, &detached_tris);
    if (detached_tris > 0) {
      params.error_message_add(
          NodeWarningType::Warning,
          fmt::format(fmt::runtime(TIP_("{} non-manifold triangles were detached, leaving seams")),
                      detached_tris));
    }
    if (result == nullptr) {
      params.error_message_add(NodeWarningType::Warning,
                               TIP_("Decimation produced no geometry; the input was kept"));
      return;
    }

    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeDecimateMesh", GEO_NODE_DECIMATE_MESH);
  ntype.ui_name = "Decimate Mesh";
  ntype.ui_description =
      "Reduce the number of triangles of a mesh, removing detail where it changes the surface "
      "the least";
  ntype.enum_name_legacy = "DECIMATE_MESH";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_decimate_mesh_cc

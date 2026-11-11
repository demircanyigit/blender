/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_cleanup.hh"
#include "GEO_randomize.hh"

#include "bmesh.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_cleanup_mesh_cc {

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to clean up");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Points to clean up, and the edges and faces made of them");

  PanelDeclarationBuilder &loose = b.add_panel("Delete Loose").default_closed(false);
  loose.add_input<decl::Bool>("Loose Vertices")
      .default_value(true)
      .description("Delete points with no edges");
  loose.add_input<decl::Bool>("Loose Edges")
      .default_value(true)
      .description("Delete edges with no faces");
  loose.add_input<decl::Bool>("Loose Faces")
      .default_value(false)
      .description("Delete faces that touch no other face");

  PanelDeclarationBuilder &remove = b.add_panel("Delete").default_closed(false);
  remove.add_input<decl::Bool>("Duplicate Faces")
      .default_value(true)
      .description("Delete faces that use the same points as another face");
  remove.add_input<decl::Bool>("Interior Faces")
      .default_value(false)
      .description("Delete faces inside the mesh, whose edges all have more than two faces");
  remove.add_input<decl::Bool>("Small Pieces")
      .default_value(false)
      .description("Delete loose parts with few faces or of a small size");
  remove.add_input<decl::Int>("Min Faces")
      .default_value(4)
      .min(0)
      .description("Delete parts with fewer faces than this");
  remove.add_input<decl::Float>("Min Size")
      .default_value(0.0f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Delete parts smaller than this. Zero turns it off");

  PanelDeclarationBuilder &dissolve = b.add_panel("Dissolve").default_closed(true);
  dissolve.add_input<decl::Bool>("Degenerate")
      .default_value(true)
      .description("Remove edges shorter than the distance, and faces with no area");
  dissolve.add_input<decl::Float>("Distance")
      .default_value(0.0001f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Edges shorter than this are removed");
  dissolve.add_input<decl::Bool>("Extra Vertices")
      .default_value(false)
      .description("Remove points in the middle of straight lines, between only two edges");
  dissolve.add_input<decl::Float>("Straight Angle")
      .default_value(DEG2RADF(1.0f))
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .description("How far from straight the line may bend");
  dissolve.add_input<decl::Bool>("Limited Dissolve")
      .default_value(false)
      .description("Merge faces where the surface stays nearly flat");
  dissolve.add_input<decl::Float>("Max Angle")
      .default_value(DEG2RADF(5.0f))
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .description("Only merge where the surface bends less than this");
  dissolve.add_input<decl::Bool>("All Boundaries")
      .default_value(false)
      .description("Also dissolve points on open boundaries, whatever their angle");
  dissolve.add_input<decl::Bool>("Keep Materials")
      .default_value(false)
      .description("Don't merge faces with different materials");
  dissolve.add_input<decl::Bool>("Keep Seams").default_value(false).description(
      "Don't dissolve seams");
  dissolve.add_input<decl::Bool>("Keep Sharp")
      .default_value(false)
      .description("Don't dissolve sharp edges");
  dissolve.add_input<decl::Bool>("Keep UVs")
      .default_value(false)
      .description("Don't dissolve edges that split UV islands");

  PanelDeclarationBuilder &faces = b.add_panel("Faces").default_closed(true);
  faces.add_input<decl::Bool>("Fill Holes").default_value(false).description("Fill holes");
  faces.add_input<decl::Int>("Max Sides")
      .default_value(4)
      .min(0)
      .description("Only fill holes with at most this many sides. Zero fills all holes");
  faces.add_input<decl::Bool>("Make Planar").default_value(false).description("Flatten faces");
  faces.add_input<decl::Float>("Factor")
      .default_value(1.0f)
      .min(-10.0f)
      .max(10.0f)
      .subtype(PROP_FACTOR)
      .description("How far to flatten in each pass");
  faces.add_input<decl::Int>("Iterations")
      .default_value(1)
      .min(1)
      .max(10000)
      .description("How many times to flatten");
  faces.add_input<decl::Bool>("Split Non-Planar")
      .default_value(false)
      .description("Split faces that bend more than the angle");
  faces.add_input<decl::Float>("Split Angle")
      .default_value(DEG2RADF(5.0f))
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .description("Split faces that bend more than this");
  faces.add_input<decl::Bool>("Split Concave")
      .default_value(false)
      .description("Split faces that are not convex");
  faces.add_input<decl::Bool>("Recalculate Normals")
      .default_value(false)
      .description("Make all faces point outward");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");

  geometry::MeshCleanupParams settings;
  settings.loose_verts = params.extract_input<bool>("Loose Vertices");
  settings.loose_edges = params.extract_input<bool>("Loose Edges");
  settings.loose_faces = params.extract_input<bool>("Loose Faces");
  settings.duplicate_faces = params.extract_input<bool>("Duplicate Faces");
  settings.interior_faces = params.extract_input<bool>("Interior Faces");
  settings.small_pieces = params.extract_input<bool>("Small Pieces");
  settings.small_pieces_faces = params.extract_input<int>("Min Faces");
  settings.small_pieces_size = params.extract_input<float>("Min Size");
  settings.degenerate = params.extract_input<bool>("Degenerate");
  settings.degenerate_distance = params.extract_input<float>("Distance");
  settings.extra_verts = params.extract_input<bool>("Extra Vertices");
  settings.extra_verts_angle = params.extract_input<float>("Straight Angle");
  settings.limited = params.extract_input<bool>("Limited Dissolve");
  settings.limited_angle = params.extract_input<float>("Max Angle");
  settings.limited_all_boundaries = params.extract_input<bool>("All Boundaries");
  int delimit = BMO_DELIM_NORMAL;
  if (params.extract_input<bool>("Keep Materials")) {
    delimit |= BMO_DELIM_MATERIAL;
  }
  if (params.extract_input<bool>("Keep Seams")) {
    delimit |= BMO_DELIM_SEAM;
  }
  if (params.extract_input<bool>("Keep Sharp")) {
    delimit |= BMO_DELIM_SHARP;
  }
  if (params.extract_input<bool>("Keep UVs")) {
    delimit |= BMO_DELIM_UV;
  }
  settings.limited_delimit = delimit;
  settings.fill_holes = params.extract_input<bool>("Fill Holes");
  settings.fill_holes_sides = params.extract_input<int>("Max Sides");
  settings.make_planar = params.extract_input<bool>("Make Planar");
  settings.make_planar_factor = params.extract_input<float>("Factor");
  settings.make_planar_iterations = params.extract_input<int>("Iterations");
  settings.split_nonplanar = params.extract_input<bool>("Split Non-Planar");
  settings.split_nonplanar_angle = params.extract_input<float>("Split Angle");
  settings.split_concave = params.extract_input<bool>("Split Concave");
  settings.recalc_normals = params.extract_input<bool>("Recalculate Normals");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    const bke::MeshFieldContext context(*src_mesh, bke::AttrDomain::Point);
    FieldEvaluator evaluator(context, src_mesh->verts_num);
    evaluator.add(selection_field);
    evaluator.evaluate();
    const VArraySpan<bool> selection = evaluator.get_evaluated<bool>(0);
    Mesh *result = geometry::mesh_cleanup(*src_mesh, selection, settings);
    if (result == nullptr) {
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

  geo_node_type_base(&ntype, "GeometryNodeCleanupMesh");
  ntype.ui_name = "Cleanup";
  ntype.ui_description =
      "Clean up a mesh: delete loose and duplicate parts, dissolve, fill holes, split and "
      "flatten faces, and fix normals";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_cleanup_mesh_cc

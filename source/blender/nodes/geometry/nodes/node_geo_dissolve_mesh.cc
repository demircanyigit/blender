/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_dissolve.hh"
#include "GEO_randomize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_dissolve_mesh_cc {

using Mode = geometry::DissolveMode;

static const EnumPropertyItem mode_items[] = {
    {int(Mode::Vertices),
     "VERTICES",
     ICON_NONE,
     N_("Vertices"),
     N_("Remove the selected points, and merge the faces around them")},
    {int(Mode::Edges),
     "EDGES",
     ICON_NONE,
     N_("Edges"),
     N_("Remove the selected edges, and merge the faces on both sides")},
    {int(Mode::Faces),
     "FACES",
     ICON_NONE,
     N_("Faces"),
     N_("Merge the selected faces that touch each other into one")},
    {int(Mode::Limited),
     "LIMITED",
     ICON_NONE,
     N_("Limited"),
     N_("Remove the selected edges and points where the surface stays nearly flat")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to remove parts from");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Bool>("Merged Faces")
      .field_on_all()
      .description("The faces made by merging. Not available in Vertices mode");

  b.add_input<decl::Menu>("Mode")
      .static_items(mode_items)
      .default_value(Mode::Edges)
      .optional_label();
  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Points, edges or faces to dissolve, depending on the mode");
  b.add_input<decl::Bool>("Remove Inline Points")
      .default_value(true)
      .description("Also remove points left between only two edges. Not used in Vertices mode");
  b.add_input<decl::Bool>("Face Split")
      .default_value(false)
      .description(
          "Split off the corners of faces to keep the surrounding shape. Only used by Vertices "
          "and Edges");
  b.add_input<decl::Bool>("Tear Boundary")
      .default_value(false)
      .usage_by_menu("Mode", int(Mode::Vertices))
      .description("Split off corners on open boundaries instead of merging the faces");
  b.add_input<decl::Float>("Angle")
      .default_value(DEG2RADF(5.0f))
      .min(0.0f)
      .max(DEG2RADF(180.0f))
      .subtype(PROP_ANGLE)
      .usage_by_menu("Mode", int(Mode::Limited))
      .description("Only dissolve where the surface bends less than this");
  b.add_input<decl::Bool>("All Boundaries")
      .default_value(false)
      .usage_by_menu("Mode", int(Mode::Limited))
      .description("Also dissolve points on open boundaries, whatever their angle");

  PanelDeclarationBuilder &keep = b.add_panel("Keep").default_closed(true);
  keep.add_input<decl::Bool>("Materials")
      .default_value(false)
      .usage_by_menu("Mode", int(Mode::Limited))
      .description("Don't merge faces with different materials");
  keep.add_input<decl::Bool>("Seams")
      .default_value(false)
      .usage_by_menu("Mode", int(Mode::Limited))
      .description("Don't dissolve seams");
  keep.add_input<decl::Bool>("Sharp Edges")
      .default_value(false)
      .usage_by_menu("Mode", int(Mode::Limited))
      .description("Don't dissolve sharp edges");
  keep.add_input<decl::Bool>("UVs")
      .default_value(false)
      .usage_by_menu("Mode", int(Mode::Limited))
      .description("Don't dissolve edges that split UV islands");
}

/** eturn The new mesh, or null if nothing is selected. */
static Mesh *dissolve_mesh(const Mesh &mesh,
                           const Field<bool> &selection_field,
                           const geometry::MeshDissolveParams &settings)
{
  const bke::AttrDomain domain = geometry::dissolve_selection_domain(settings.mode);
  const bke::MeshFieldContext context(mesh, domain);
  FieldEvaluator evaluator(context, mesh.attributes().domain_size(domain));
  evaluator.add(selection_field);
  evaluator.evaluate();
  const VArraySpan<bool> selection = evaluator.get_evaluated<bool>(0);
  return geometry::mesh_dissolve(mesh, selection, settings);
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection = params.extract_input<Field<bool>>("Selection");

  geometry::MeshDissolveParams settings;
  settings.mode = params.extract_input<Mode>("Mode");
  settings.remove_inline_points = params.extract_input<bool>("Remove Inline Points");
  settings.face_split = params.extract_input<bool>("Face Split");
  settings.tear_boundary = params.extract_input<bool>("Tear Boundary");
  settings.angle = std::max(params.extract_input<float>("Angle"), 0.0f);
  settings.all_boundaries = params.extract_input<bool>("All Boundaries");
  settings.keep_materials = params.extract_input<bool>("Materials");
  settings.keep_seams = params.extract_input<bool>("Seams");
  settings.keep_sharp = params.extract_input<bool>("Sharp Edges");
  settings.keep_uvs = params.extract_input<bool>("UVs");
  settings.merged_faces_id = params.get_output_anonymous_attribute_id_if_needed("Merged Faces");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    Mesh *result = dissolve_mesh(*src_mesh, selection, settings);
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

  geo_node_type_base(&ntype, "GeometryNodeDissolveMesh");
  ntype.ui_name = "Dissolve";
  ntype.ui_description =
      "Remove points, edges or faces and merge the faces around them, without leaving holes";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_dissolve_mesh_cc

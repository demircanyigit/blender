/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_customdata.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_randomize.hh"

#include "bmesh.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_dissolve_mesh_cc {

enum class Mode {
  Vertices = 0,
  Edges = 1,
  Faces = 2,
  Limited = 3,
};

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

struct Settings {
  Mode mode;
  bool remove_inline_points;
  bool face_split;
  bool tear_boundary;
  float angle;
  bool all_boundaries;
  int delimit;
  std::optional<std::string> merged_faces_id;
};

static bke::AttrDomain selection_domain(const Mode mode)
{
  switch (mode) {
    case Mode::Vertices:
      return bke::AttrDomain::Point;
    case Mode::Edges:
    case Mode::Limited:
      return bke::AttrDomain::Edge;
    case Mode::Faces:
      return bke::AttrDomain::Face;
  }
  BLI_assert_unreachable();
  return bke::AttrDomain::Point;
}

/** Tag the points whose edges are all selected, because Limited mode also takes points. */
static void tag_verts_of_tagged_edges(BMesh *bm)
{
  BMIter iter;
  BMVert *vert;
  BM_ITER_MESH (vert, &iter, bm, BM_VERTS_OF_MESH) {
    bool all_tagged = vert->e != nullptr;
    BMIter edge_iter;
    BMEdge *edge;
    BM_ITER_ELEM (edge, &edge_iter, vert, BM_EDGES_OF_VERT) {
      if (!BM_elem_flag_test(edge, BM_ELEM_TAG)) {
        all_tagged = false;
        break;
      }
    }
    BM_elem_flag_set(vert, BM_ELEM_TAG, all_tagged);
  }
}

/** \return The new mesh, or null if nothing is selected. */
static Mesh *dissolve_mesh(const Mesh &mesh,
                           const Field<bool> &selection_field,
                           const Settings &settings)
{
  const bke::AttrDomain domain = selection_domain(settings.mode);
  const bke::MeshFieldContext context(mesh, domain);
  FieldEvaluator evaluator(context, mesh.attributes().domain_size(domain));
  evaluator.add(selection_field);
  evaluator.evaluate();
  const VArraySpan<bool> selection = evaluator.get_evaluated<bool>(0);
  if (!selection.contains(true)) {
    return nullptr;
  }

  BMeshCreateParams create_params{};
  create_params.use_toolflags = true;
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);

  BM_mesh_elem_hflag_disable_all(bm, BM_VERT | BM_EDGE | BM_FACE, BM_ELEM_TAG, false);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE | BM_FACE);
  for (const int i : selection.index_range()) {
    if (!selection[i]) {
      continue;
    }
    switch (domain) {
      case bke::AttrDomain::Point:
        BM_elem_flag_enable(BM_vert_at_index(bm, i), BM_ELEM_TAG);
        break;
      case bke::AttrDomain::Edge:
        BM_elem_flag_enable(BM_edge_at_index(bm, i), BM_ELEM_TAG);
        break;
      default:
        BM_elem_flag_enable(BM_face_at_index(bm, i), BM_ELEM_TAG);
        break;
    }
  }

  int merged_offset = -1;
  if (settings.merged_faces_id) {
    const char *name = settings.merged_faces_id->c_str();
    BM_data_layer_add_named(bm, &bm->pdata, CD_PROP_BOOL, name);
    merged_offset = CustomData_get_offset_named(&bm->pdata, CD_PROP_BOOL, name);
  }
  if (merged_offset != -1 && settings.mode == Mode::Edges) {
    /* The operator doesn't output the faces it merges. A merged face gets the attributes of one of
     * the faces it was made from, so the value set here carries over. */
    BMIter iter;
    BMEdge *edge;
    BM_ITER_MESH (edge, &iter, bm, BM_EDGES_OF_MESH) {
      BMLoop *l_a, *l_b;
      if (BM_elem_flag_test(edge, BM_ELEM_TAG) && BM_edge_loop_pair(edge, &l_a, &l_b)) {
        BM_ELEM_CD_SET_BOOL(l_a->f, merged_offset, true);
        BM_ELEM_CD_SET_BOOL(l_b->f, merged_offset, true);
      }
    }
  }

  BMOperator op;
  switch (settings.mode) {
    case Mode::Vertices:
      BMO_op_initf(bm,
                   &op,
                   0,
                   "dissolve_verts verts=%hv use_face_split=%b use_boundary_tear=%b",
                   BM_ELEM_TAG,
                   settings.face_split,
                   settings.tear_boundary);
      break;
    case Mode::Edges:
      BMO_op_initf(bm,
                   &op,
                   0,
                   "dissolve_edges edges=%he use_verts=%b use_face_split=%b",
                   BM_ELEM_TAG,
                   settings.remove_inline_points,
                   settings.face_split);
      break;
    case Mode::Faces:
      BMO_op_initf(bm,
                   &op,
                   0,
                   "dissolve_faces faces=%hf use_verts=%b",
                   BM_ELEM_TAG,
                   settings.remove_inline_points);
      break;
    case Mode::Limited:
      tag_verts_of_tagged_edges(bm);
      BMO_op_initf(
          bm,
          &op,
          0,
          "dissolve_limit edges=%he verts=%hv angle_limit=%f use_dissolve_boundaries=%b delimit=%i",
          BM_ELEM_TAG,
          BM_ELEM_TAG,
          settings.angle,
          settings.all_boundaries,
          settings.delimit);
      break;
  }
  BMO_op_exec(bm, &op);
  /* Errors from the operators can stay on the mesh, and aren't useful here. */
  BMO_error_clear(bm);

  if (merged_offset != -1 && ELEM(settings.mode, Mode::Faces, Mode::Limited)) {
    BMOIter iter;
    BMFace *face;
    BMO_ITER (face, &iter, op.slots_out, "region.out", BM_FACE) {
      BM_ELEM_CD_SET_BOOL(face, merged_offset, true);
    }
  }
  BMO_op_finish(bm, &op);

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);
  return result;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection = params.extract_input<Field<bool>>("Selection");

  Settings settings;
  settings.mode = params.extract_input<Mode>("Mode");
  settings.remove_inline_points = params.extract_input<bool>("Remove Inline Points");
  settings.face_split = params.extract_input<bool>("Face Split");
  settings.tear_boundary = params.extract_input<bool>("Tear Boundary");
  settings.angle = std::max(params.extract_input<float>("Angle"), 0.0f);
  settings.all_boundaries = params.extract_input<bool>("All Boundaries");
  settings.delimit = BMO_DELIM_NORMAL;
  if (params.extract_input<bool>("Materials")) {
    settings.delimit |= BMO_DELIM_MATERIAL;
  }
  if (params.extract_input<bool>("Seams")) {
    settings.delimit |= BMO_DELIM_SEAM;
  }
  if (params.extract_input<bool>("Sharp Edges")) {
    settings.delimit |= BMO_DELIM_SHARP;
  }
  if (params.extract_input<bool>("UVs")) {
    settings.delimit |= BMO_DELIM_UV;
  }
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

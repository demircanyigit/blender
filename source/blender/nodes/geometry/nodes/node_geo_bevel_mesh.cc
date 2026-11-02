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
#include "bmesh_tools.hh"

#include "NOD_rna_define.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_bevel_mesh_cc {

enum class Affect {
  Vertices = BEVEL_AFFECT_VERTICES,
  Edges = BEVEL_AFFECT_EDGES,
};

enum class WidthType {
  Offset = BEVEL_AMT_OFFSET,
  Width = BEVEL_AMT_WIDTH,
  Depth = BEVEL_AMT_DEPTH,
  Percent = BEVEL_AMT_PERCENT,
  Absolute = BEVEL_AMT_ABSOLUTE,
};

enum class Miter {
  Sharp = BEVEL_MITER_SHARP,
  Patch = BEVEL_MITER_PATCH,
  Arc = BEVEL_MITER_ARC,
};

enum class Intersection {
  GridFill = BEVEL_VMESH_ADJ,
  Cutoff = BEVEL_VMESH_CUTOFF,
};

static const EnumPropertyItem affect_items[] = {
    {int(Affect::Edges), "EDGES", ICON_NONE, N_("Edges"), N_("Bevel the edges")},
    {int(Affect::Vertices),
     "VERTICES",
     ICON_NONE,
     N_("Vertices"),
     N_("Bevel only the vertices, not the edges")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem width_type_items[] = {
    {int(WidthType::Offset),
     "OFFSET",
     ICON_NONE,
     N_("Offset"),
     N_("Amount is the distance from the edge to the new edges")},
    {int(WidthType::Width), "WIDTH", ICON_NONE, N_("Width"), N_("Amount is the width of the bevel")},
    {int(WidthType::Depth),
     "DEPTH",
     ICON_NONE,
     N_("Depth"),
     N_("Amount is the distance from the bevel face to the original edge")},
    {int(WidthType::Percent),
     "PERCENT",
     ICON_NONE,
     N_("Percent"),
     N_("Amount is a percentage of the length of the neighboring edges")},
    {int(WidthType::Absolute),
     "ABSOLUTE",
     ICON_NONE,
     N_("Absolute"),
     N_("Amount is the distance along the neighboring edges")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem miter_outer_items[] = {
    {int(Miter::Sharp), "SHARP", ICON_NONE, N_("Sharp"), N_("Outside of the corner is sharp")},
    {int(Miter::Patch), "PATCH", ICON_NONE, N_("Patch"), N_("Outside of the corner is squared off")},
    {int(Miter::Arc), "ARC", ICON_NONE, N_("Arc"), N_("Outside of the corner is rounded")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem miter_inner_items[] = {
    {int(Miter::Sharp), "SHARP", ICON_NONE, N_("Sharp"), N_("Inside of the corner is sharp")},
    {int(Miter::Arc), "ARC", ICON_NONE, N_("Arc"), N_("Inside of the corner is rounded")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem intersection_items[] = {
    {int(Intersection::GridFill),
     "GRID_FILL",
     ICON_NONE,
     N_("Grid Fill"),
     N_("Fill the place where beveled edges meet with a grid")},
    {int(Intersection::Cutoff),
     "CUTOFF",
     ICON_NONE,
     N_("Cutoff"),
     N_("Cut off the place where beveled edges meet with a flat face")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to bevel");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Bool>("Bevel Faces").field_on_all().description("The faces made by the bevel");

  b.add_input<decl::Menu>("Affect")
      .static_items(affect_items)
      .default_value(Affect::Edges)
      .optional_label();
  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Edges or vertices to bevel, depending on Affect");
  b.add_input<decl::Menu>("Width Type")
      .static_items(width_type_items)
      .default_value(WidthType::Offset)
      .description("How the amount is measured");
  b.add_input<decl::Float>("Amount")
      .default_value(0.1f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .field_on_all()
      .description("Size of the bevel for each edge or vertex. Zero doesn't bevel it");
  b.add_input<decl::Int>("Segments").default_value(1).min(1).max(1000);
  b.add_input<decl::Float>("Shape")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Shape of the bevel. 0.5 is round, less is concave and more is convex");
  b.add_input<decl::Float>("Angle")
      .default_value(DEG2RADF(30.0f))
      .min(0.0f)
      .max(DEG2RADF(180.0f))
      .subtype(PROP_ANGLE)
      .usage_by_menu("Affect", int(Affect::Edges))
      .description(
          "Only bevel edges whose faces meet at more than this angle. Zero bevels every selected "
          "edge");
  b.add_input<decl::Float>("Weight")
      .default_value(1.0f)
      .min(0.0f)
      .field_on_all()
      .description("Multiplier of the amount for each edge or vertex. Zero doesn't bevel it");

  PanelDeclarationBuilder &geometry = b.add_panel("Geometry").default_closed(true);
  geometry.add_input<decl::Menu>("Miter Outer")
      .static_items(miter_outer_items)
      .default_value(Miter::Sharp)
      .usage_by_menu("Affect", int(Affect::Edges));
  geometry.add_input<decl::Menu>("Miter Inner")
      .static_items(miter_inner_items)
      .default_value(Miter::Sharp)
      .usage_by_menu("Affect", int(Affect::Edges));
  geometry.add_input<decl::Float>("Spread")
      .default_value(0.1f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Affect", int(Affect::Edges))
      .description("Spread of the arc miters");
  geometry.add_input<decl::Menu>("Intersections")
      .static_items(intersection_items)
      .default_value(Intersection::GridFill);
  geometry.add_input<decl::Bool>("Clamp Overlap")
      .default_value(true)
      .description("Make the bevel smaller where it would overlap itself");
  geometry.add_input<decl::Bool>("Loop Slide")
      .default_value(true)
      .description("Keep the new vertices on the neighboring edges, even if widths get uneven");

  PanelDeclarationBuilder &shading = b.add_panel("Shading").default_closed(true);
  shading.add_input<decl::Bool>("Harden Normals")
      .default_value(false)
      .description("Match the normals of the new faces to the faces next to them");
  shading.add_input<decl::Bool>("Mark Seam").default_value(false);
  shading.add_input<decl::Bool>("Mark Sharp").default_value(false);
  shading.add_input<decl::Int>("Material Index")
      .default_value(-1)
      .min(-1)
      .description("Material of the new faces. -1 uses the material of the faces next to them");
}

struct Settings {
  Affect affect;
  WidthType width_type;
  int segments;
  float shape;
  float angle;
  Miter miter_outer;
  Miter miter_inner;
  float spread;
  Intersection intersection;
  bool clamp_overlap;
  bool loop_slide;
  bool harden_normals;
  bool mark_seam;
  bool mark_sharp;
  int material_index;
  std::optional<std::string> bevel_faces_id;
};

/** Name of the temporary layer with the weights, which #BM_mesh_bevel reads. */
static constexpr const char *weight_layer_name = ".bevel_node_weight";

/** \return The new mesh, or null if nothing is beveled. */
static Mesh *bevel_mesh(const Mesh &mesh,
                        const Field<bool> &selection_field,
                        const Field<float> &amount_field,
                        const Field<float> &weight_field,
                        const Settings &settings)
{
  const bool affect_edges = settings.affect == Affect::Edges;
  const bke::AttrDomain domain = affect_edges ? bke::AttrDomain::Edge : bke::AttrDomain::Point;
  const bke::MeshFieldContext context(mesh, domain);
  FieldEvaluator evaluator(context, mesh.attributes().domain_size(domain));
  evaluator.set_selection(selection_field);
  evaluator.add(amount_field);
  evaluator.add(weight_field);
  evaluator.evaluate();
  const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
  if (selection.is_empty()) {
    return nullptr;
  }
  const VArray<float> amounts = evaluator.get_evaluated<float>(0);
  const VArray<float> weights = evaluator.get_evaluated<float>(1);

  BMeshCreateParams create_params{};
  BMeshFromMeshParams convert_params{};
  convert_params.calc_face_normal = true;
  convert_params.calc_vert_normal = true;
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);
  BM_mesh_elem_table_ensure(bm, BM_VERT | BM_EDGE);

  CustomData &weight_data = affect_edges ? bm->edata : bm->vdata;
  BM_data_layer_add_named(bm, &weight_data, CD_PROP_FLOAT, weight_layer_name);
  const int weight_offset = CustomData_get_offset_named(
      &weight_data, CD_PROP_FLOAT, weight_layer_name);

  /* Same threshold as the modifier. */
  const float angle_threshold = cosf(settings.angle + 0.000000175f);
  float max_amount = 0.0f;
  selection.foreach_index([&](const int i) {
    const float amount = amounts[i] * weights[i];
    if (!(amount > 0.0f)) {
      return;
    }
    if (affect_edges) {
      BMEdge *edge = BM_edge_at_index(bm, i);
      BMLoop *l_a, *l_b;
      /* The bevel only supports manifold edges. */
      if (!BM_edge_loop_pair(edge, &l_a, &l_b)) {
        return;
      }
      if (settings.angle > 0.0f && dot_v3v3(l_a->f->no, l_b->f->no) >= angle_threshold) {
        return;
      }
      BM_ELEM_CD_SET_FLOAT(edge, weight_offset, amount);
      BM_elem_flag_enable(edge, BM_ELEM_TAG);
      BM_elem_flag_enable(edge->v1, BM_ELEM_TAG);
      BM_elem_flag_enable(edge->v2, BM_ELEM_TAG);
    }
    else {
      BMVert *vert = BM_vert_at_index(bm, i);
      BM_ELEM_CD_SET_FLOAT(vert, weight_offset, amount);
      BM_elem_flag_enable(vert, BM_ELEM_TAG);
    }
    max_amount = std::max(max_amount, amount);
  });

  if (max_amount == 0.0f) {
    BM_mesh_free(bm);
    return nullptr;
  }

  /* The bevel takes one amount, and a weight in the layer that multiplies it. */
  selection.foreach_index([&](const int i) {
    BMElem *elem = affect_edges ? reinterpret_cast<BMElem *>(BM_edge_at_index(bm, i)) :
                                  reinterpret_cast<BMElem *>(BM_vert_at_index(bm, i));
    BM_ELEM_CD_SET_FLOAT(elem, weight_offset, BM_ELEM_CD_GET_FLOAT(elem, weight_offset) / max_amount);
  });

  BM_mesh_bevel(bm,
                max_amount,
                int(settings.width_type),
                BEVEL_PROFILE_SUPERELLIPSE,
                settings.segments,
                settings.shape,
                bool(settings.affect),
                true,
                settings.clamp_overlap,
                nullptr,
                -1,
                std::clamp(settings.material_index, -1, std::max(int(mesh.totcol) - 1, -1)),
                settings.loop_slide,
                settings.mark_seam,
                settings.mark_sharp,
                settings.harden_normals,
                BEVEL_FACE_STRENGTH_NONE,
                int(settings.miter_outer),
                int(settings.miter_inner),
                settings.spread,
                nullptr,
                int(settings.intersection),
                affect_edges ? -1 : weight_offset,
                affect_edges ? weight_offset : -1);

  /* The bevel tags the faces it makes. */
  Array<bool> bevel_faces;
  if (settings.bevel_faces_id) {
    bevel_faces.reinitialize(bm->totface);
    BMIter iter;
    BMFace *face;
    int i;
    BM_ITER_MESH_INDEX (face, &iter, bm, BM_FACES_OF_MESH, i) {
      bevel_faces[i] = BM_elem_flag_test_bool(face, BM_ELEM_TAG);
    }
  }

  BM_data_layer_free_named(bm, &weight_data, weight_layer_name);
  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);

  if (settings.bevel_faces_id) {
    result->attributes_for_write().add<bool>(
        *settings.bevel_faces_id,
        bke::AttrDomain::Face,
        bke::AttributeInitVArray(VArray<bool>::from_span(bevel_faces)));
  }
  return result;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection = params.extract_input<Field<bool>>("Selection");
  const Field<float> amount = params.extract_input<Field<float>>("Amount");
  const Field<float> weight = params.extract_input<Field<float>>("Weight");

  Settings settings;
  settings.affect = params.extract_input<Affect>("Affect");
  settings.width_type = params.extract_input<WidthType>("Width Type");
  settings.segments = std::clamp(params.extract_input<int>("Segments"), 1, 1000);
  settings.shape = std::clamp(params.extract_input<float>("Shape"), 0.0f, 1.0f);
  settings.angle = std::max(params.extract_input<float>("Angle"), 0.0f);
  settings.miter_outer = params.extract_input<Miter>("Miter Outer");
  settings.miter_inner = params.extract_input<Miter>("Miter Inner");
  settings.spread = std::max(params.extract_input<float>("Spread"), 0.0f);
  settings.intersection = params.extract_input<Intersection>("Intersections");
  settings.clamp_overlap = params.extract_input<bool>("Clamp Overlap");
  settings.loop_slide = params.extract_input<bool>("Loop Slide");
  settings.harden_normals = params.extract_input<bool>("Harden Normals");
  settings.mark_seam = params.extract_input<bool>("Mark Seam");
  settings.mark_sharp = params.extract_input<bool>("Mark Sharp");
  settings.material_index = params.extract_input<int>("Material Index");
  settings.bevel_faces_id = params.get_output_anonymous_attribute_id_if_needed("Bevel Faces");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    Mesh *result = bevel_mesh(*src_mesh, selection, amount, weight, settings);
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

  geo_node_type_base(&ntype, "GeometryNodeBevelMesh");
  ntype.ui_name = "Bevel";
  ntype.ui_description = "Bevel the edges or vertices of a mesh, like the Bevel modifier";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_bevel_mesh_cc

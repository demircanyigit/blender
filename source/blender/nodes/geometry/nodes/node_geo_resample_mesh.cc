/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BLI_math_vector.hh"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_randomize.hh"

#include "bmesh.hh"

#include "NOD_rna_define.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_resample_mesh_cc {

enum class Mode {
  Length = 0,
  Count = 1,
};

static const EnumPropertyItem mode_items[] = {
    {int(Mode::Length),
     "LENGTH",
     ICON_NONE,
     N_("Length"),
     N_("Split every edge into even segments that are no longer than a length")},
    {int(Mode::Count),
     "COUNT",
     ICON_NONE,
     N_("Count"),
     N_("Split every edge into a number of even segments")},
    {0, nullptr, 0, nullptr, nullptr},
};

/** Keeps a very small length from making a mesh that doesn't fit into memory. */
static constexpr int64_t max_new_points = 100'000'000;

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to add points to along its edges");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Bool>("New Points")
      .field_on_all()
      .description("The points that were added along the edges");

  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Edges to split");
  b.add_input<decl::Menu>("Mode")
      .static_items(mode_items)
      .default_value(Mode::Length)
      .optional_label();
  b.add_input<decl::Float>("Length")
      .default_value(0.1f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .field_on_all()
      .usage_by_single_menu(int(Mode::Length))
      .description("Longest length of a segment. Edges aren't split where this is zero");
  b.add_input<decl::Int>("Count")
      .default_value(2)
      .min(1)
      .field_on_all()
      .usage_by_single_menu(int(Mode::Count))
      .description("Number of segments of each edge");
}

/** The number of points to add to each edge. */
static Array<int> calc_edge_cuts(const Mesh &mesh,
                                 const Mode mode,
                                 const Field<bool> &selection_field,
                                 const Field<float> &length_field,
                                 const Field<int> &count_field)
{
  const bke::MeshFieldContext context(mesh, bke::AttrDomain::Edge);
  FieldEvaluator evaluator(context, mesh.edges_num);
  evaluator.set_selection(selection_field);
  evaluator.add(length_field);
  evaluator.add(count_field);
  evaluator.evaluate();
  const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
  const VArray<float> lengths = evaluator.get_evaluated<float>(0);
  const VArray<int> counts = evaluator.get_evaluated<int>(1);

  const Span<float3> positions = mesh.vert_positions();
  const Span<int2> edges = mesh.edges();

  Array<int> cuts(mesh.edges_num, 0);
  selection.foreach_index(GrainSize(4096), [&](const int i) {
    if (mode == Mode::Count) {
      cuts[i] = std::max(counts[i] - 1, 0);
      return;
    }
    const float segment_length = lengths[i];
    if (segment_length <= 0.0f) {
      return;
    }
    const double edge_length = math::distance(positions[edges[i][0]], positions[edges[i][1]]);
    /* The small margin keeps an edge of exactly N lengths from getting an extra segment. */
    const double segments = std::ceil(edge_length / double(segment_length) - 1e-5);
    cuts[i] = int(std::clamp(segments - 1.0, 0.0, double(max_new_points)));
  });
  return cuts;
}

static Mesh *resample_mesh(const Mesh &mesh,
                           const Span<int> cuts,
                           const std::optional<std::string> &new_points_id)
{
  BMeshCreateParams create_params{};
  BMeshFromMeshParams convert_params{};
  BMesh *bm = BKE_mesh_to_bmesh_ex(&mesh, &create_params, &convert_params);
  BM_mesh_elem_table_ensure(bm, BM_EDGE);

  /* Splitting adds edges, so the table doesn't stay valid. */
  Array<BMEdge *> edges(mesh.edges_num);
  for (const int i : edges.index_range()) {
    edges[i] = BM_edge_at_index(bm, i);
  }

  /* Attributes are interpolated by the split. */
  Vector<BMVert *> new_verts;
  for (const int i : edges.index_range()) {
    if (cuts[i] == 0) {
      continue;
    }
    new_verts.resize(cuts[i]);
    BM_edge_split_n(bm, edges[i], cuts[i], new_verts.data());
    for (BMVert *vert : new_verts) {
      BM_elem_flag_enable(vert, BM_ELEM_TAG);
    }
  }

  Array<bool> new_points;
  if (new_points_id) {
    new_points.reinitialize(bm->totvert);
    BMIter iter;
    BMVert *vert;
    int i;
    BM_ITER_MESH_INDEX (vert, &iter, bm, BM_VERTS_OF_MESH, i) {
      new_points[i] = BM_elem_flag_test_bool(vert, BM_ELEM_TAG);
    }
  }

  Mesh *result = BKE_mesh_from_bmesh_for_eval_nomain(bm, nullptr, &mesh);
  BM_mesh_free(bm);

  if (new_points_id) {
    result->attributes_for_write().add<bool>(
        *new_points_id,
        bke::AttrDomain::Point,
        bke::AttributeInitVArray(VArray<bool>::from_span(new_points)));
  }
  return result;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection = params.extract_input<Field<bool>>("Selection");
  const Mode mode = params.extract_input<Mode>("Mode");
  const Field<float> length = params.extract_input<Field<float>>("Length");
  const Field<int> count = params.extract_input<Field<int>>("Count");
  const std::optional<std::string> new_points_id =
      params.get_output_anonymous_attribute_id_if_needed("New Points");

  std::atomic<bool> too_many_points = false;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->edges_num == 0) {
      return;
    }
    const Array<int> cuts = calc_edge_cuts(*src_mesh, mode, selection, length, count);
    int64_t total_cuts = 0;
    for (const int edge_cuts : cuts) {
      total_cuts += edge_cuts;
    }
    if (total_cuts == 0) {
      return;
    }
    if (total_cuts > max_new_points) {
      too_many_points = true;
      return;
    }
    Mesh *result = resample_mesh(*src_mesh, cuts, new_points_id);
    geometry::debug_randomize_mesh_order(result);
    geometry_set.replace_mesh(result);
  });

  if (too_many_points) {
    params.error_message_add(NodeWarningType::Warning,
                             TIP_("The segments are so short that the mesh would get too big"));
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeResampleMesh");
  ntype.ui_name = "Resample Mesh";
  ntype.ui_description =
      "Add points along the edges of a mesh without changing its shape or its number of faces";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_resample_mesh_cc

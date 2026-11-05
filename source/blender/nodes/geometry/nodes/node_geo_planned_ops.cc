/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * Nodes for the per-element, topology and deform operations that also exist as modifiers:
 * Randomize Elements, Exploded View, Element Order, Element Pivot, Mark by Angle, Noise Deform,
 * FFD, Panelize, Inset, Poke, Beautify Faces, Rotate Edges, Symmetrize, Push, Melt and Smart UV
 * Project. Both use the same code in the geometry module, so they give the same result.
 */

#include "DNA_mesh_types.h"
#include "DNA_pointcloud_types.h"

#include "BLI_function_ref.hh"

#include "BKE_attribute.hh"
#include "BKE_curves.hh"
#include "BKE_mesh.hh"
#include "BKE_pointcloud.hh"

#include "GEO_deform_ops.hh"
#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_elements.hh"
#include "GEO_mesh_topology_ops.hh"
#include "GEO_randomize.hh"
#include "GEO_uv_smart_project.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_planned_ops_cc {

/* -------------------------------------------------------------------- */
/** \name Shared
 * \{ */

static const EnumPropertyItem axis_items[] = {
    {0, "X", ICON_NONE, N_("X"), ""},
    {1, "Y", ICON_NONE, N_("Y"), ""},
    {2, "Z", ICON_NONE, N_("Z"), ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem element_items[] = {
    {int(geometry::MeshElement::Face), "FACE", ICON_NONE, N_("Face"), N_("Each face on its own")},
    {int(geometry::MeshElement::Island), "ISLAND", ICON_NONE, N_("Island"), N_("Each connected piece")},
    {int(geometry::MeshElement::UVIsland), "UV_ISLAND", ICON_NONE, N_("UV Island"), N_("Each UV island")},
    {int(geometry::MeshElement::Material), "MATERIAL", ICON_NONE, N_("Material"), N_("All faces with the same material")},
    {int(geometry::MeshElement::FaceSet), "FACE_SET", ICON_NONE, N_("Face Set"), N_("Each face set from sculpt mode")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem pivot_items[] = {
    {int(geometry::ElementPivot::Center), "CENTER", ICON_NONE, N_("Center"), N_("Average of the element's points")},
    {int(geometry::ElementPivot::BoundsCenter), "BOUNDS_CENTER", ICON_NONE, N_("Bounds Center"), N_("Center of the element's bounding box")},
    {int(geometry::ElementPivot::Origin), "ORIGIN", ICON_NONE, N_("Origin"), N_("Origin of the geometry")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void declare_mesh(NodeDeclarationBuilder &b, const char *description)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();
  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description(description);
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
}

static void declare_geometry(NodeDeclarationBuilder &b, const char *description)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();
  b.add_input<decl::Geometry>("Geometry")
      .supported_type({GeometryComponent::Type::Mesh,
                       GeometryComponent::Type::Curve,
                       GeometryComponent::Type::PointCloud})
      .is_default_link_socket()
      .description(description);
  b.add_output<decl::Geometry>("Geometry").propagate_all().align_with_previous();
}

static void declare_selection(NodeDeclarationBuilder &b, const char *description)
{
  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description(description);
}

static void declare_weight(NodeDeclarationBuilder &b)
{
  b.add_input<decl::Float>("Weight")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .field_on_all()
      .description("How much each point is affected");
}

static void declare_element(NodeDeclarationBuilder &b, const char *description)
{
  b.add_input<decl::Menu>("Element")
      .static_items(element_items)
      .default_value(MenuValue(int(geometry::MeshElement::Island)))
      .description(description);
  b.add_input<decl::String>("UV Map")
      .usage_by_menu("Element", int(geometry::MeshElement::UVIsland))
      .description("UV map for UV islands. The active one when empty");
}

static Array<float> evaluate_on(const fn::FieldContext &context,
                                const int size,
                                const Field<float> &field)
{
  Array<float> values(size);
  fn::FieldEvaluator evaluator{context, size};
  evaluator.add_with_destination(field, values.as_mutable_span());
  evaluator.evaluate();
  return values;
}

static Array<float> point_weights(const Mesh &mesh, const Field<float> &field)
{
  const bke::MeshFieldContext context{mesh, bke::AttrDomain::Point};
  return evaluate_on(context, mesh.verts_num, field);
}

static IndexMask face_selection(const Mesh &mesh,
                                const Field<bool> &field,
                                IndexMaskMemory &memory)
{
  const bke::MeshFieldContext context{mesh, bke::AttrDomain::Face};
  Array<bool> selection(mesh.faces_num);
  fn::FieldEvaluator evaluator{context, mesh.faces_num};
  evaluator.add_with_destination(field, selection.as_mutable_span());
  evaluator.evaluate();
  return IndexMask::from_bools(selection, memory);
}

/** Run \a fn on the positions of every mesh, curves and point cloud, with the weight field. */
static void deform_geometry(GeometrySet &geometry_set,
                            const Field<float> &weight_field,
                            const FunctionRef<void(MutableSpan<float3>, Span<float>)> fn)
{
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    if (geometry_set.has_mesh()) {
      Mesh &mesh = *geometry_set.get_mesh_for_write();
      const Array<float> weights = point_weights(mesh, weight_field);
      fn(mesh.vert_positions_for_write(), weights);
      mesh.tag_positions_changed();
    }
    if (geometry_set.has_curves()) {
      Curves &curves_id = *geometry_set.get_curves_for_write();
      bke::CurvesGeometry &curves = curves_id.geometry.wrap();
      const bke::CurvesFieldContext context{curves_id, bke::AttrDomain::Point};
      const Array<float> weights = evaluate_on(context, curves.points_num(), weight_field);
      fn(curves.positions_for_write(), weights);
      curves.tag_positions_changed();
    }
    if (geometry_set.has_pointcloud()) {
      PointCloud &points = *geometry_set.get_pointcloud_for_write();
      const bke::PointCloudFieldContext context{points};
      const Array<float> weights = evaluate_on(context, points.totpoint, weight_field);
      fn(points.positions_for_write(), weights);
      points.tag_positions_changed();
    }
  });
}

/** Replace each mesh with the result of \a fn, when it gives one. */
static void replace_meshes(GeometrySet &geometry_set, const FunctionRef<Mesh *(const Mesh &)> fn)
{
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *mesh = geometry_set.get_mesh();
    if (mesh == nullptr) {
      return;
    }
    if (Mesh *result = fn(*mesh)) {
      geometry::debug_randomize_mesh_order(result);
      geometry_set.replace_mesh(result);
    }
  });
}

static void register_node(blender::bke::bNodeType &ntype,
                          const char *idname,
                          const char *ui_name,
                          const char *description,
                          void (*declare)(NodeDeclarationBuilder &),
                          void (*exec)(GeoNodeExecParams))
{
  geo_node_type_base(&ntype, idname);
  ntype.ui_name = ui_name;
  ntype.ui_description = description;
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = declare;
  ntype.geometry_node_execute = exec;
  blender::bke::node_register_type(ntype);
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Randomize Elements
 * \{ */

static const EnumPropertyItem color_mode_items[] = {
    {int(geometry::ElementColorMode::RandomHue), "HUE", ICON_NONE, N_("Random Hue"), N_("A random hue")},
    {int(geometry::ElementColorMode::Grayscale), "GRAYSCALE", ICON_NONE, N_("Grayscale"), N_("A random gray")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void randomize_elements_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose elements to randomize");
  b.add_output<decl::Float>("Random").field_on_all().description(
      "A random value from 0 to 1 for each element, on the faces");
  declare_weight(b);
  declare_element(b, "What counts as one element");
  b.add_input<decl::Int>("Seed").description("Change it for a different random result");

  b.add_input<decl::Bool>("Geometry").default_value(true).description(
      "Move, rotate and scale the elements");
  b.add_input<decl::Menu>("Pivot")
      .static_items(pivot_items)
      .default_value(MenuValue(int(geometry::ElementPivot::Center)))
      .description("Point each element rotates and scales around");
  b.add_input<decl::Vector>("Rotation")
      .subtype(PROP_EULER)
      .description("Largest rotation around X, Y and Z, both ways");
  b.add_input<decl::Float>("Normal Rotation")
      .subtype(PROP_ANGLE)
      .description("Largest rotation around the element's normal, both ways");
  b.add_input<decl::Float>("Scale Min").default_value(1.0f).description("Smallest random scale");
  b.add_input<decl::Float>("Scale Max").default_value(1.0f).description("Largest random scale");
  b.add_input<decl::Bool>("Uniform Scale")
      .default_value(true)
      .description("Scale all axes by the same amount");
  b.add_input<decl::Vector>("Offset")
      .subtype(PROP_TRANSLATION)
      .description("Largest move on X, Y and Z, both ways");
  b.add_input<decl::Float>("Normal Offset")
      .subtype(PROP_DISTANCE)
      .description("Largest move along the element's normal, both ways");

  b.add_input<decl::Bool>("UVs").default_value(false).description(
      "Rotate, scale and move the UVs of each element. Uses the UV Map input");
  b.add_input<decl::Float>("UV Rotation").subtype(PROP_ANGLE).description(
      "Largest UV rotation, both ways");
  b.add_input<decl::Float>("UV Scale Min").default_value(1.0f);
  b.add_input<decl::Float>("UV Scale Max").default_value(1.0f);
  b.add_input<decl::Vector>("UV Offset").subtype(PROP_XYZ).description(
      "Largest UV move on U and V, both ways");

  b.add_input<decl::Bool>("Color").default_value(false).description(
      "Give each element a random color");
  b.add_input<decl::String>("Color Attribute")
      .default_value("Color")
      .description("Color attribute to write. It is added when missing");
  b.add_input<decl::Menu>("Colors")
      .static_items(color_mode_items)
      .default_value(MenuValue(int(geometry::ElementColorMode::RandomHue)))
      .description("What kind of colors to pick");
  b.add_input<decl::Float>("Saturation").default_value(0.8f).min(0.0f).max(1.0f).subtype(
      PROP_FACTOR);
  b.add_input<decl::Float>("Value Min").default_value(0.5f).min(0.0f).max(1.0f).subtype(
      PROP_FACTOR);
  b.add_input<decl::Float>("Value Max").default_value(1.0f).min(0.0f).max(1.0f).subtype(
      PROP_FACTOR);
  b.add_input<decl::Float>("Blend")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the new colors replace the colors there already");
}

static void randomize_elements_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<float> weight_field = params.extract_input<Field<float>>("Weight");
  geometry::RandomizeElementsParams rp;
  rp.element = params.extract_input<geometry::MeshElement>("Element");
  rp.uv_map = params.extract_input<std::string>("UV Map");
  rp.seed = params.extract_input<int>("Seed");
  rp.use_geometry = params.extract_input<bool>("Geometry");
  rp.pivot = params.extract_input<geometry::ElementPivot>("Pivot");
  rp.rotation = params.extract_input<float3>("Rotation");
  rp.normal_rotation = params.extract_input<float>("Normal Rotation");
  rp.scale_min = params.extract_input<float>("Scale Min");
  rp.scale_max = params.extract_input<float>("Scale Max");
  rp.uniform_scale = params.extract_input<bool>("Uniform Scale");
  rp.offset = params.extract_input<float3>("Offset");
  rp.normal_offset = params.extract_input<float>("Normal Offset");
  rp.use_uvs = params.extract_input<bool>("UVs");
  rp.uv_rotation = params.extract_input<float>("UV Rotation");
  rp.uv_scale_min = params.extract_input<float>("UV Scale Min");
  rp.uv_scale_max = params.extract_input<float>("UV Scale Max");
  rp.uv_offset = params.extract_input<float3>("UV Offset").xy();
  rp.use_color = params.extract_input<bool>("Color");
  rp.color_attribute = params.extract_input<std::string>("Color Attribute");
  rp.color_mode = params.extract_input<geometry::ElementColorMode>("Colors");
  rp.saturation = params.extract_input<float>("Saturation");
  rp.value_min = params.extract_input<float>("Value Min");
  rp.value_max = params.extract_input<float>("Value Max");
  rp.color_blend = params.extract_input<float>("Blend");
  rp.random_id = params.get_output_anonymous_attribute_id_if_needed("Random");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    const Array<float> weights = point_weights(mesh, weight_field);
    geometry::RandomizeElementsParams mesh_params = rp;
    mesh_params.weights = weights;
    return geometry::randomize_elements(mesh, mesh_params);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Exploded View
 * \{ */

static const EnumPropertyItem explode_direction_items[] = {
    {int(geometry::ExplodeDirection::FromCenter), "FROM_CENTER", ICON_NONE, N_("From Center"), N_("Away from the center point")},
    {int(geometry::ExplodeDirection::Normal), "NORMAL", ICON_NONE, N_("Normal"), N_("Along the normal of each element")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void exploded_view_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to push apart");
  declare_weight(b);
  declare_element(b, "What moves as one piece");
  b.add_input<decl::Menu>("Direction")
      .static_items(explode_direction_items)
      .default_value(MenuValue(int(geometry::ExplodeDirection::FromCenter)))
      .description("Which way the elements move");
  b.add_input<decl::Vector>("Center")
      .subtype(PROP_TRANSLATION)
      .usage_by_menu("Direction", int(geometry::ExplodeDirection::FromCenter))
      .description("Point the elements move away from");
  b.add_input<decl::Float>("Factor").default_value(1.0f).subtype(PROP_FACTOR).description(
      "How far the explosion has gone. Animate it from 0 to 1");
  b.add_input<decl::Float>("Distance").default_value(1.0f).subtype(PROP_DISTANCE).description(
      "How far the elements move at a factor of 1");
  b.add_input<decl::Float>("Randomness").min(0.0f).max(1.0f).subtype(PROP_FACTOR).description(
      "How much the distance varies between elements");
  b.add_input<decl::Float>("Tumble").subtype(PROP_ANGLE).description(
      "Largest random rotation of each element at a factor of 1");
  b.add_input<decl::Int>("Seed");
}

static void exploded_view_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<float> weight_field = params.extract_input<Field<float>>("Weight");
  geometry::ExplodedViewParams ep;
  ep.element = params.extract_input<geometry::MeshElement>("Element");
  ep.uv_map = params.extract_input<std::string>("UV Map");
  ep.direction = params.extract_input<geometry::ExplodeDirection>("Direction");
  ep.center = params.extract_input<float3>("Center");
  ep.factor = params.extract_input<float>("Factor");
  ep.distance = params.extract_input<float>("Distance");
  ep.randomness = params.extract_input<float>("Randomness");
  ep.tumble = params.extract_input<float>("Tumble");
  ep.seed = params.extract_input<int>("Seed");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    const Array<float> weights = point_weights(mesh, weight_field);
    geometry::ExplodedViewParams mesh_params = ep;
    mesh_params.weights = weights;
    return geometry::exploded_view(mesh, mesh_params);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Element Order
 * \{ */

static const EnumPropertyItem order_items[] = {
    {int(geometry::ElementOrderMode::Random), "RANDOM", ICON_NONE, N_("Random"), N_("A random order")},
    {int(geometry::ElementOrderMode::Axis), "AXIS", ICON_NONE, N_("Axis"), N_("Along an axis")},
    {int(geometry::ElementOrderMode::Distance), "DISTANCE", ICON_NONE, N_("Distance"), N_("Nearest to a point first")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void element_order_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose faces to reorder");
  declare_element(b, "What stays together in the new order");
  b.add_input<decl::Menu>("Order")
      .static_items(order_items)
      .default_value(MenuValue(int(geometry::ElementOrderMode::Random)))
      .description("How to order the elements");
  b.add_input<decl::Menu>("Axis")
      .static_items(axis_items)
      .default_value(MenuValue(2))
      .usage_by_menu("Order", int(geometry::ElementOrderMode::Axis))
      .description("Axis to order along");
  b.add_input<decl::Vector>("Point")
      .subtype(PROP_TRANSLATION)
      .usage_by_menu("Order", int(geometry::ElementOrderMode::Distance))
      .description("Point to measure the distance from");
  b.add_input<decl::Int>("Seed").usage_by_menu("Order", int(geometry::ElementOrderMode::Random));
  b.add_input<decl::Bool>("Reverse").default_value(false);
}

static void element_order_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  geometry::ElementOrderParams op;
  op.element = params.extract_input<geometry::MeshElement>("Element");
  op.uv_map = params.extract_input<std::string>("UV Map");
  op.mode = params.extract_input<geometry::ElementOrderMode>("Order");
  op.axis = params.extract_input<MenuValue>("Axis").value;
  op.point = params.extract_input<float3>("Point");
  op.seed = params.extract_input<int>("Seed");
  op.reverse = params.extract_input<bool>("Reverse");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    if (const Mesh *mesh = geometry_set.get_mesh()) {
      if (Mesh *result = geometry::element_order(*mesh, op)) {
        geometry_set.replace_mesh(result);
      }
    }
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Element Pivot
 * \{ */

static void element_pivot_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose elements to measure");
  b.add_output<decl::Vector>("Center").field_on_all().description(
      "Pivot of the element of each point");
  b.add_output<decl::Vector>("X Axis").field_on_all().description(
      "Along the longest edge of the element, flat to its normal");
  b.add_output<decl::Vector>("Z Axis").field_on_all().description("Normal of the element");
  b.add_output<decl::Int>("Index").field_on_all().description("Number of the element");
  declare_element(b, "What counts as one element");
  b.add_input<decl::Menu>("Pivot")
      .static_items(pivot_items)
      .default_value(MenuValue(int(geometry::ElementPivot::Center)))
      .description("Which point to give as the center");
}

static void element_pivot_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  geometry::ElementPivotParams pp;
  pp.element = params.extract_input<geometry::MeshElement>("Element");
  pp.uv_map = params.extract_input<std::string>("UV Map");
  pp.pivot = params.extract_input<geometry::ElementPivot>("Pivot");
  pp.center_name = params.get_output_anonymous_attribute_id_if_needed("Center").value_or("");
  pp.x_axis_name = params.get_output_anonymous_attribute_id_if_needed("X Axis").value_or("");
  pp.z_axis_name = params.get_output_anonymous_attribute_id_if_needed("Z Axis").value_or("");
  pp.index_name = params.get_output_anonymous_attribute_id_if_needed("Index").value_or("");
  if (!pp.center_name.empty() || !pp.x_axis_name.empty() || !pp.z_axis_name.empty() ||
      !pp.index_name.empty())
  {
    geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
      if (Mesh *mesh = geometry_set.get_mesh_for_write()) {
        geometry::store_element_pivot(*mesh, pp);
      }
    });
  }
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Mark by Angle
 * \{ */

static const EnumPropertyItem angle_side_items[] = {
    {int(geometry::MarkAngleSide::Both), "BOTH", ICON_NONE, N_("Both"), N_("Mark convex and concave edges")},
    {int(geometry::MarkAngleSide::Convex), "CONVEX", ICON_NONE, N_("Convex"), N_("Mark only edges that stick out")},
    {int(geometry::MarkAngleSide::Concave), "CONCAVE", ICON_NONE, N_("Concave"), N_("Mark only edges that sink in")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void mark_by_angle_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose edges to mark");
  b.add_output<decl::Bool>("Marked").field_on_all().description("The edges that were marked");
  declare_selection(b, "Edges that can be changed");
  b.add_input<decl::Float>("Angle")
      .default_value(DEG2RADF(30.0f))
      .min(0.0f)
      .max(float(M_PI))
      .subtype(PROP_ANGLE)
      .description("Edges where the faces meet at a steeper angle are marked");
  b.add_input<decl::Menu>("Edges")
      .static_items(angle_side_items)
      .default_value(MenuValue(int(geometry::MarkAngleSide::Both)))
      .description("Which steep edges to mark");
  b.add_input<decl::Bool>("Sharp").default_value(true).description("Mark sharp edges");
  b.add_input<decl::Bool>("Seam").default_value(false).description("Mark UV seams");
  b.add_input<decl::Bool>("Crease").default_value(false).description("Set the edge crease");
  b.add_input<decl::Bool>("Bevel Weight").default_value(false).description(
      "Set the edge bevel weight");
  b.add_input<decl::Float>("Value").default_value(1.0f).min(0.0f).max(1.0f).subtype(
      PROP_FACTOR);
  b.add_input<decl::Bool>("Open Edges").default_value(false).description(
      "Also mark edges with only one face");
  b.add_input<decl::Bool>("Clear Others").default_value(true).description(
      "Clear the marks of the other edges, instead of keeping them");
  b.add_input<decl::Bool>("Flat Faces").default_value(false).description(
      "Shade the faces around marked edges flat");
}

static void mark_by_angle_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::MarkByAngleParams mp;
  mp.angle = params.extract_input<float>("Angle");
  mp.side = params.extract_input<geometry::MarkAngleSide>("Edges");
  mp.mark_sharp = params.extract_input<bool>("Sharp");
  mp.mark_seam = params.extract_input<bool>("Seam");
  mp.mark_crease = params.extract_input<bool>("Crease");
  mp.mark_bevel_weight = params.extract_input<bool>("Bevel Weight");
  mp.value = params.extract_input<float>("Value");
  mp.use_boundary = params.extract_input<bool>("Open Edges");
  mp.clear_others = params.extract_input<bool>("Clear Others");
  mp.flat_faces = params.extract_input<bool>("Flat Faces");
  const std::optional<std::string> marked_id = params.get_output_anonymous_attribute_id_if_needed(
      "Marked");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *mesh = geometry_set.get_mesh();
    if (mesh == nullptr || mesh->edges_num == 0) {
      return;
    }
    const bke::MeshFieldContext context{*mesh, bke::AttrDomain::Edge};
    fn::FieldEvaluator evaluator{context, mesh->edges_num};
    evaluator.set_selection(selection_field);
    evaluator.evaluate();
    const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
    Array<bool> marked;
    if (marked_id) {
      marked.reinitialize(mesh->edges_num);
      geometry::mesh_edges_by_angle(*mesh, mp, marked);
      Array<bool> in_selection(mesh->edges_num, false);
      selection.to_bools(in_selection);
      for (const int edge : marked.index_range()) {
        marked[edge] = marked[edge] && in_selection[edge];
      }
    }
    Mesh *result = geometry::mesh_mark_by_angle(*mesh, selection, mp);
    if (result == nullptr && marked_id) {
      result = BKE_mesh_copy_for_eval(*mesh);
    }
    if (result == nullptr) {
      return;
    }
    if (marked_id) {
      bke::MutableAttributeAccessor attributes = result->attributes_for_write();
      attributes.remove(*marked_id);
      attributes.add<bool>(*marked_id,
                           bke::AttrDomain::Edge,
                           bke::AttributeInitVArray(VArray<bool>::from_container(std::move(marked))));
    }
    geometry_set.replace_mesh(result);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Noise Deform
 * \{ */

static void noise_deform_declare(NodeDeclarationBuilder &b)
{
  declare_geometry(b, "Geometry whose points to move");
  declare_weight(b);
  b.add_input<decl::Vector>("Strength")
      .default_value({0.0f, 0.0f, 0.2f})
      .subtype(PROP_TRANSLATION)
      .description("Largest move on X, Y and Z");
  b.add_input<decl::Float>("Scale").default_value(1.0f).min(0.0001f).description(
      "Size of the noise pattern");
  b.add_input<decl::Float>("Phase").description("Moves through the noise. Animate it for motion");
  b.add_input<decl::Int>("Seed");
  b.add_input<decl::Bool>("Fractal").default_value(false).description("Add finer layers of noise");
  b.add_input<decl::Float>("Detail").default_value(2.0f).min(0.0f).max(15.0f);
  b.add_input<decl::Float>("Roughness").default_value(0.5f).min(0.0f).max(1.0f).subtype(
      PROP_FACTOR);
  b.add_input<decl::Float>("Lacunarity").default_value(2.0f).min(0.0f).max(1000.0f);
}

static void noise_deform_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  const Field<float> weight_field = params.extract_input<Field<float>>("Weight");
  geometry::NoiseDeformParams np;
  np.strength = params.extract_input<float3>("Strength");
  np.scale = params.extract_input<float>("Scale");
  np.phase = params.extract_input<float>("Phase");
  np.seed = params.extract_input<int>("Seed");
  np.fractal = params.extract_input<bool>("Fractal");
  np.detail = params.extract_input<float>("Detail");
  np.roughness = params.extract_input<float>("Roughness");
  np.lacunarity = params.extract_input<float>("Lacunarity");
  deform_geometry(geometry_set, weight_field, [&](MutableSpan<float3> positions, Span<float> w) {
    geometry::noise_deform(positions, w, np);
  });
  params.set_output("Geometry", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name FFD
 * \{ */

static void ffd_declare(NodeDeclarationBuilder &b)
{
  declare_geometry(b, "Geometry to bend with the lattice");
  declare_weight(b);
  b.add_input<decl::Geometry>("Control Points")
      .only_realized_data()
      .description(
          "Points of the bent lattice, Resolution cubed of them with X counting fastest. Make "
          "them from a Points grid and move them");
  b.add_input<decl::Int>("Resolution").default_value(2).min(2).max(4).description(
      "Number of control points on each side");
  b.add_input<decl::Vector>("Box Min")
      .default_value({-1.0f, -1.0f, -1.0f})
      .subtype(PROP_TRANSLATION)
      .description("Lowest corner of the rest lattice");
  b.add_input<decl::Vector>("Box Max")
      .default_value({1.0f, 1.0f, 1.0f})
      .subtype(PROP_TRANSLATION)
      .description("Highest corner of the rest lattice");
  b.add_input<decl::Bool>("Inside Only").default_value(false).description(
      "Only change points inside the box");
}

static void ffd_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  const GeometrySet points_set = params.extract_input<GeometrySet>("Control Points");
  const Field<float> weight_field = params.extract_input<Field<float>>("Weight");
  geometry::FFDParams fp;
  fp.resolution = std::clamp(params.extract_input<int>("Resolution"), 2, 4);
  fp.bounds_min = params.extract_input<float3>("Box Min");
  fp.bounds_max = params.extract_input<float3>("Box Max");
  fp.inside_only = params.extract_input<bool>("Inside Only");

  Span<float3> control_positions;
  if (const PointCloud *points = points_set.get_pointcloud()) {
    control_positions = points->positions();
  }
  else if (const Mesh *mesh = points_set.get_mesh()) {
    control_positions = mesh->vert_positions();
  }
  const int count = fp.resolution * fp.resolution * fp.resolution;
  if (control_positions.size() < count) {
    params.error_message_add(NodeWarningType::Info,
                             TIP_("The control points are fewer than Resolution cubed"));
    params.set_output("Geometry", std::move(geometry_set));
    return;
  }
  Array<float3> offsets(64, float3(0.0f));
  for (const int i : IndexRange(count)) {
    offsets[i] = control_positions[i] - geometry::ffd_rest_point(fp, i);
  }
  fp.offsets = offsets;
  deform_geometry(geometry_set, weight_field, [&](MutableSpan<float3> positions, Span<float> w) {
    geometry::ffd_deform(positions, w, fp);
  });
  params.set_output("Geometry", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Panelize, Inset and Poke
 * \{ */

static void panelize_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to split into panels");
  b.add_output<decl::Bool>("Panels").field_on_all().description("The faces of the panels");
  b.add_output<decl::Bool>("Gaps").field_on_all().description("The faces in the gaps");
  declare_selection(b, "Faces to panelize");
  b.add_input<decl::Float>("Gap").default_value(0.02f).min(0.0f).subtype(PROP_DISTANCE).description(
      "Width of the gap between panels");
  b.add_input<decl::Float>("Chamfer").default_value(0.01f).min(0.0f).subtype(PROP_DISTANCE).description(
      "Width of the slope on the edge of each panel");
  b.add_input<decl::Float>("Height").default_value(0.01f).subtype(PROP_DISTANCE).description(
      "How far the panels stand out, or sink when negative");
  b.add_input<decl::Bool>("Even").default_value(true).description(
      "Keep the gap the same width at sharp corners");
  b.add_input<decl::Bool>("Relative").default_value(false).description(
      "Scale the gap by the size of each face");
}

static void panelize_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::PanelizeParams pp;
  pp.gap = params.extract_input<float>("Gap");
  pp.bevel = params.extract_input<float>("Chamfer");
  pp.height = params.extract_input<float>("Height");
  pp.use_even_offset = params.extract_input<bool>("Even");
  pp.use_relative_offset = params.extract_input<bool>("Relative");
  geometry::TopologyOutputs outputs;
  outputs.top = params.get_output_anonymous_attribute_id_if_needed("Panels").value_or("");
  outputs.side = params.get_output_anonymous_attribute_id_if_needed("Gaps").value_or("");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    IndexMaskMemory memory;
    return geometry::mesh_panelize(mesh, face_selection(mesh, selection_field, memory), pp, outputs);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void inset_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose faces to inset");
  b.add_output<decl::Bool>("Top").field_on_all().description("The inset faces");
  b.add_output<decl::Bool>("Side").field_on_all().description("The new rim faces");
  declare_selection(b, "Faces to inset");
  b.add_input<decl::Bool>("Individual").default_value(false).description(
      "Inset each face on its own");
  b.add_input<decl::Float>("Thickness").default_value(0.1f).min(0.0f).subtype(PROP_DISTANCE).description(
      "How far the edges move in");
  b.add_input<decl::Float>("Depth").subtype(PROP_DISTANCE).description(
      "How far the inner faces move out");
  b.add_input<decl::Bool>("Even").default_value(true);
  b.add_input<decl::Bool>("Relative").default_value(false);
  b.add_input<decl::Bool>("Boundary").default_value(true).description("Inset open edges too");
  b.add_input<decl::Bool>("Edge Rail").default_value(false).description("Slide along the edges");
  b.add_input<decl::Bool>("Outset").default_value(false).description(
      "Grow outward instead of inward");
}

static void inset_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::InsetParams ip;
  ip.individual = params.extract_input<bool>("Individual");
  ip.thickness = params.extract_input<float>("Thickness");
  ip.depth = params.extract_input<float>("Depth");
  ip.use_even_offset = params.extract_input<bool>("Even");
  ip.use_relative_offset = params.extract_input<bool>("Relative");
  ip.use_boundary = params.extract_input<bool>("Boundary");
  ip.use_edge_rail = params.extract_input<bool>("Edge Rail");
  ip.use_outset = params.extract_input<bool>("Outset");
  geometry::TopologyOutputs outputs;
  outputs.top = params.get_output_anonymous_attribute_id_if_needed("Top").value_or("");
  outputs.side = params.get_output_anonymous_attribute_id_if_needed("Side").value_or("");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    IndexMaskMemory memory;
    return geometry::mesh_inset(mesh, face_selection(mesh, selection_field, memory), ip, outputs);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static const EnumPropertyItem poke_center_items[] = {
    {int(geometry::PokeCenter::MeanWeighted), "MEAN_WEIGHTED", ICON_NONE, N_("Weighted Mean"), N_("Average of the points, weighted by edge length")},
    {int(geometry::PokeCenter::Mean), "MEAN", ICON_NONE, N_("Mean"), N_("Average of the points")},
    {int(geometry::PokeCenter::Bounds), "BOUNDS", ICON_NONE, N_("Bounds"), N_("Center of the bounding box")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void poke_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose faces to poke");
  b.add_output<decl::Bool>("New Faces").field_on_all().description("The triangles of the fans");
  declare_selection(b, "Faces to poke");
  b.add_input<decl::Menu>("Center")
      .static_items(poke_center_items)
      .default_value(MenuValue(int(geometry::PokeCenter::MeanWeighted)))
      .description("Where the new point goes");
  b.add_input<decl::Float>("Offset").subtype(PROP_DISTANCE).description(
      "Move the new point along the normal");
  b.add_input<decl::Bool>("Relative").default_value(false).description(
      "Scale the offset by the size of each face");
}

static void poke_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::PokeParams pp;
  pp.center = params.extract_input<geometry::PokeCenter>("Center");
  pp.offset = params.extract_input<float>("Offset");
  pp.use_relative_offset = params.extract_input<bool>("Relative");
  geometry::TopologyOutputs outputs;
  outputs.side = params.get_output_anonymous_attribute_id_if_needed("New Faces").value_or("");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    IndexMaskMemory memory;
    return geometry::mesh_poke(mesh, face_selection(mesh, selection_field, memory), pp, outputs);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Beautify Faces, Rotate Edges and Symmetrize
 * \{ */

static const EnumPropertyItem beautify_items[] = {
    {int(geometry::BeautifyMethod::Area), "AREA", ICON_NONE, N_("Area"), N_("Even out the size of the triangles")},
    {int(geometry::BeautifyMethod::Angle), "ANGLE", ICON_NONE, N_("Angle"), N_("Even out the angles of the triangles")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void beautify_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose triangles to even out");
  declare_selection(b, "Faces that can change");
  b.add_input<decl::Menu>("Method")
      .static_items(beautify_items)
      .default_value(MenuValue(int(geometry::BeautifyMethod::Area)))
      .optional_label();
  b.add_input<decl::Bool>("Triangulate").default_value(false).description(
      "Split the other faces into triangles first, so they can be beautified too");
}

static void beautify_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  const geometry::BeautifyMethod method = params.extract_input<geometry::BeautifyMethod>("Method");
  const bool triangulate = params.extract_input<bool>("Triangulate");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    IndexMaskMemory memory;
    return geometry::mesh_beautify(
        mesh, face_selection(mesh, selection_field, memory), method, triangulate);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void rotate_edges_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose edges to rotate");
  declare_selection(b, "Edges between two selected faces are rotated");
  b.add_input<decl::Int>("Steps").default_value(1).min(1).max(1000);
  b.add_input<decl::Bool>("Counter Clockwise").default_value(false);
}

static void rotate_edges_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  const int steps = params.extract_input<int>("Steps");
  const bool ccw = params.extract_input<bool>("Counter Clockwise");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    IndexMaskMemory memory;
    return geometry::mesh_rotate_edges(
        mesh, face_selection(mesh, selection_field, memory), ccw, steps);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static const EnumPropertyItem symmetrize_items[] = {
    {int(geometry::SymmetrizeDirection::NegativeX), "NEGATIVE_X", ICON_NONE, N_("-X to +X"), ""},
    {int(geometry::SymmetrizeDirection::NegativeY), "NEGATIVE_Y", ICON_NONE, N_("-Y to +Y"), ""},
    {int(geometry::SymmetrizeDirection::NegativeZ), "NEGATIVE_Z", ICON_NONE, N_("-Z to +Z"), ""},
    {int(geometry::SymmetrizeDirection::PositiveX), "POSITIVE_X", ICON_NONE, N_("+X to -X"), ""},
    {int(geometry::SymmetrizeDirection::PositiveY), "POSITIVE_Y", ICON_NONE, N_("+Y to -Y"), ""},
    {int(geometry::SymmetrizeDirection::PositiveZ), "POSITIVE_Z", ICON_NONE, N_("+Z to -Z"), ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static void symmetrize_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to make symmetrical");
  b.add_input<decl::Menu>("Direction")
      .static_items(symmetrize_items)
      .default_value(MenuValue(int(geometry::SymmetrizeDirection::NegativeX)))
      .description("Which side to copy to the other");
  b.add_input<decl::Float>("Merge Distance")
      .default_value(0.0001f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Points this close to the middle are merged");
}

static void symmetrize_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const geometry::SymmetrizeDirection direction =
      params.extract_input<geometry::SymmetrizeDirection>("Direction");
  const float distance = params.extract_input<float>("Merge Distance");
  replace_meshes(geometry_set, [&](const Mesh &mesh) {
    return geometry::mesh_symmetrize(mesh, direction, distance);
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Push and Melt
 * \{ */

static void push_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to inflate or shrink");
  declare_weight(b);
  b.add_input<decl::Float>("Distance").default_value(0.1f).subtype(PROP_DISTANCE).description(
      "How far to move along the normals. Negative shrinks");
}

static void push_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<float> weight_field = params.extract_input<Field<float>>("Weight");
  const float distance = params.extract_input<float>("Distance");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    if (!geometry_set.has_mesh() || distance == 0.0f) {
      return;
    }
    Mesh &mesh = *geometry_set.get_mesh_for_write();
    const Array<float> weights = point_weights(mesh, weight_field);
    const Array<float3> normals(mesh.vert_normals());
    geometry::push_deform(mesh.vert_positions_for_write(), normals, weights, distance);
    mesh.tag_positions_changed();
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void melt_declare(NodeDeclarationBuilder &b)
{
  declare_geometry(b, "Geometry to melt");
  declare_weight(b);
  b.add_input<decl::Float>("Amount").min(0.0f).max(1.0f).subtype(PROP_FACTOR).description(
      "How far it has melted");
  b.add_input<decl::Float>("Spread").default_value(0.2f).min(0.0f).description(
      "How far the melted part spreads out");
  b.add_input<decl::Float>("Solidity")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Low melts like ice cream, high holds the top together longer");
  b.add_input<decl::Menu>("Axis").static_items(axis_items).default_value(MenuValue(2)).description(
      "Axis that points up");
  b.add_input<decl::Bool>("Flip").default_value(false).description(
      "Melt toward the other end of the axis");
}

static void melt_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  const Field<float> weight_field = params.extract_input<Field<float>>("Weight");
  geometry::MeltParams mp;
  mp.amount = params.extract_input<float>("Amount");
  mp.spread = params.extract_input<float>("Spread");
  mp.solidity = params.extract_input<float>("Solidity");
  mp.axis = params.extract_input<MenuValue>("Axis").value;
  mp.flip = params.extract_input<bool>("Flip");
  deform_geometry(geometry_set, weight_field, [&](MutableSpan<float3> positions, Span<float> w) {
    geometry::melt_deform(positions, w, mp);
  });
  params.set_output("Geometry", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Smart UV Project
 * \{ */

static void smart_uv_project_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to unwrap");
  b.add_output<decl::Vector>("UV").field_on_all().description(
      "The UV map, on the face corners. Store it with a Store Named Attribute node");
  declare_selection(b, "Faces to unwrap");
  b.add_input<decl::Float>("Angle Limit")
      .default_value(DEG2RADF(66.0f))
      .min(0.0f)
      .max(float(M_PI_2))
      .subtype(PROP_ANGLE)
      .description("Faces whose normals differ by less than this are projected together");
  b.add_input<decl::Float>("Margin").default_value(0.001f).min(0.0f).max(1.0f).description(
      "Space between the islands");
  b.add_input<decl::Bool>("Rotate").default_value(true).description(
      "Rotate the islands to pack them better");
  b.add_input<decl::Bool>("Pack").default_value(true).description(
      "Fit the islands into the UV square. Without it they keep their real size");
}

static void smart_uv_project_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::SmartUVProjectParams sp;
  sp.angle_limit = params.extract_input<float>("Angle Limit");
  sp.margin = params.extract_input<float>("Margin");
  sp.rotate = params.extract_input<bool>("Rotate");
  sp.pack = params.extract_input<bool>("Pack");
  const std::optional<std::string> uv_id = params.get_output_anonymous_attribute_id_if_needed(
      "UV");
  if (uv_id) {
    geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
      Mesh *mesh = geometry_set.get_mesh_for_write();
      if (mesh == nullptr || mesh->faces_num == 0) {
        return;
      }
      IndexMaskMemory memory;
      const IndexMask selection = face_selection(*mesh, selection_field, memory);
      Array<float2> uvs(mesh->corners_num, float2(0.0f));
      geometry::smart_uv_project(*mesh, selection, sp, uvs);
      Array<float3> uvs3(uvs.size());
      for (const int i : uvs.index_range()) {
        uvs3[i] = float3(uvs[i].x, uvs[i].y, 0.0f);
      }
      bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
      attributes.remove(*uv_id);
      attributes.add<float3>(
          *uv_id,
          bke::AttrDomain::Corner,
          bke::AttributeInitVArray(VArray<float3>::from_container(std::move(uvs3))));
    });
  }
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

static void node_register()
{
  static blender::bke::bNodeType randomize_elements, exploded_view, element_order, element_pivot,
      mark_by_angle, noise_deform, ffd, panelize, inset, poke, beautify, rotate_edges, symmetrize,
      push, melt, smart_uv_project;
  register_node(randomize_elements,
                "GeometryNodeRandomizeElements",
                "Randomize Elements",
                "Random rotation, scale and offset, UVs or color for each face, island or other "
                "element",
                randomize_elements_declare,
                randomize_elements_exec);
  register_node(exploded_view,
                "GeometryNodeExplodedView",
                "Exploded View",
                "Push faces, islands or other elements apart, with a factor that can be animated",
                exploded_view_declare,
                exploded_view_exec);
  register_node(element_order,
                "GeometryNodeElementOrder",
                "Element Order",
                "Reorder faces or islands, keeping the faces of each element together",
                element_order_declare,
                element_order_exec);
  register_node(element_pivot,
                "GeometryNodeElementPivot",
                "Element Pivot",
                "The center and axes of the island or other element of each point",
                element_pivot_declare,
                element_pivot_exec);
  register_node(mark_by_angle,
                "GeometryNodeMarkByAngle",
                "Mark by Angle",
                "Mark sharp edges, seams, creases or bevel weights where faces meet at a steep "
                "angle",
                mark_by_angle_declare,
                mark_by_angle_exec);
  register_node(noise_deform,
                "GeometryNodeNoiseDeform",
                "Noise Deform",
                "Move points by a fractal noise on X, Y and Z",
                noise_deform_declare,
                noise_deform_exec);
  register_node(ffd,
                "GeometryNodeFFD",
                "FFD",
                "Bend geometry with a box lattice of 2, 3 or 4 control points per side",
                ffd_declare,
                ffd_exec);
  register_node(panelize,
                "GeometryNodePanelize",
                "Panelize",
                "Split faces into panels with a gap and a chamfer between them",
                panelize_declare,
                panelize_exec);
  register_node(inset,
                "GeometryNodeInset",
                "Inset",
                "Inset faces, each on its own or as one region",
                inset_declare,
                inset_exec);
  register_node(poke,
                "GeometryNodePoke",
                "Poke",
                "Split faces into a fan of triangles around their center",
                poke_declare,
                poke_exec);
  register_node(beautify,
                "GeometryNodeBeautifyFaces",
                "Beautify Faces",
                "Flip the edges between triangles so they are more even",
                beautify_declare,
                beautify_exec);
  register_node(rotate_edges,
                "GeometryNodeRotateEdges",
                "Rotate Edges",
                "Turn the edges between faces to connect the other corners",
                rotate_edges_declare,
                rotate_edges_exec);
  register_node(symmetrize,
                "GeometryNodeSymmetrize",
                "Symmetrize",
                "Make one side of a mesh a mirror of the other",
                symmetrize_declare,
                symmetrize_exec);
  register_node(push,
                "GeometryNodePush",
                "Push",
                "Inflate or shrink a mesh along its normals",
                push_declare,
                push_exec);
  register_node(melt,
                "GeometryNodeMelt",
                "Melt",
                "Sag geometry and spread it out toward the ground",
                melt_declare,
                melt_exec);
  register_node(smart_uv_project,
                "GeometryNodeSmartUVProject",
                "Smart UV Project",
                "Unwrap a mesh automatically: faces are projected from a few directions and packed",
                smart_uv_project_declare,
                smart_uv_project_exec);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_planned_ops_cc

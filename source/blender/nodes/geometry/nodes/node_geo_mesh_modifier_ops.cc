/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * Nodes for the mesh operations that also exist as modifiers: Cap Holes, UVW Map, Tension,
 * Radial Array, Volume Select, Material By Element, Data Channel, Face Extrude and Quadrangulate. Both use the
 * same code in the geometry module, so they give the same result.
 */

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"

#include "FN_multi_function_builder.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_cap_holes.hh"
#include "GEO_mesh_channels.hh"
#include "GEO_mesh_face_extrude.hh"
#include "GEO_mesh_quadrangulate.hh"
#include "GEO_radial_array.hh"
#include "GEO_randomize.hh"
#include "GEO_uvw_map.hh"
#include "GEO_volume_select.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_mesh_modifier_ops_cc {

static const EnumPropertyItem axis_items[] = {
    {0, "X", ICON_NONE, N_("X"), ""},
    {1, "Y", ICON_NONE, N_("Y"), ""},
    {2, "Z", ICON_NONE, N_("Z"), ""},
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

/** Store \a values on \a mesh as a new attribute named \a id, replacing any old one. */
template<typename T>
static void store_attribute(Mesh &mesh,
                            const StringRef id,
                            const bke::AttrDomain domain,
                            Array<T> values)
{
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  attributes.remove(id);
  attributes.add<T>(
      id, domain, bke::AttributeInitVArray(VArray<T>::from_container(std::move(values))));
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

/* -------------------------------------------------------------------- */
/** \name Cap Holes
 * \{ */

static void cap_holes_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose holes to close");
  b.add_output<decl::Bool>("New Faces").field_on_all().description("The faces that close the holes");
  b.add_input<decl::Int>("Max Sides").default_value(0).min(0).description(
      "Only fill holes with at most this many sides. Zero fills all holes");
  b.add_input<decl::Bool>("Triangulate")
      .default_value(false)
      .description("Split the new faces into triangles");
}

static void cap_holes_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  geometry::CapHolesParams cap_params;
  cap_params.max_sides = params.extract_input<int>("Max Sides");
  cap_params.triangulate = params.extract_input<bool>("Triangulate");
  cap_params.new_faces_id = params.get_output_anonymous_attribute_id_if_needed("New Faces");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *mesh = geometry_set.get_mesh();
    if (mesh == nullptr) {
      return;
    }
    if (Mesh *result = geometry::mesh_cap_holes(*mesh, cap_params)) {
      geometry::debug_randomize_mesh_order(result);
      geometry_set.replace_mesh(result);
    }
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name UVW Map
 * \{ */

static const EnumPropertyItem uvw_type_items[] = {
    {int(geometry::UVWMapType::Planar), "PLANAR", ICON_NONE, N_("Planar"), N_("Project along the Z axis of the gizmo")},
    {int(geometry::UVWMapType::Cylindrical), "CYLINDRICAL", ICON_NONE, N_("Cylindrical"), N_("Wrap around the Z axis of the gizmo")},
    {int(geometry::UVWMapType::Spherical), "SPHERICAL", ICON_NONE, N_("Spherical"), N_("Wrap around a sphere")},
    {int(geometry::UVWMapType::Box), "BOX", ICON_NONE, N_("Box"), N_("Project each face from the side of the box it faces")},
    {int(geometry::UVWMapType::Face), "FACE", ICON_NONE, N_("Face"), N_("Give each face the whole texture")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void uvw_map_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to make a UV map for");
  b.add_output<decl::Vector>("UV").field_on_all().description(
      "The UV map, on the face corners. Store it with a Store Named Attribute node");
  b.add_input<decl::Menu>("Mapping")
      .static_items(uvw_type_items)
      .default_value(MenuValue(int(geometry::UVWMapType::Planar)))
      .optional_label();
  b.add_input<decl::Vector>("Center").subtype(PROP_TRANSLATION).description("Center of the gizmo");
  b.add_input<decl::Vector>("Rotation").subtype(PROP_EULER).description("Rotation of the gizmo");
  b.add_input<decl::Vector>("Size")
      .default_value({2.0f, 2.0f, 2.0f})
      .subtype(PROP_XYZ)
      .description("Size of the gizmo on each axis");
  b.add_input<decl::Vector>("Tile")
      .default_value({1.0f, 1.0f, 0.0f})
      .subtype(PROP_XYZ)
      .description("Number of times the texture repeats on U and V");
  b.add_input<decl::Vector>("Offset").subtype(PROP_XYZ).description("Move the map on U and V");
  b.add_input<decl::Bool>("Cap")
      .default_value(false)
      .usage_by_menu("Mapping", int(geometry::UVWMapType::Cylindrical))
      .description("Map the faces at the ends of a cylinder with a planar projection");
  b.add_input<decl::Bool>("Flip U").default_value(false);
  b.add_input<decl::Bool>("Flip V").default_value(false);
}

static void uvw_map_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  geometry::UVWMapParams uvw;
  uvw.type = params.extract_input<geometry::UVWMapType>("Mapping");
  uvw.center = params.extract_input<float3>("Center");
  uvw.rotation = params.extract_input<float3>("Rotation");
  uvw.size = params.extract_input<float3>("Size");
  const float3 tile = params.extract_input<float3>("Tile");
  const float3 offset = params.extract_input<float3>("Offset");
  uvw.tile = float2(tile.x, tile.y);
  uvw.offset = float2(offset.x, offset.y);
  uvw.cap = params.extract_input<bool>("Cap");
  uvw.flip_u = params.extract_input<bool>("Flip U");
  uvw.flip_v = params.extract_input<bool>("Flip V");
  const std::optional<std::string> uv_id = params.get_output_anonymous_attribute_id_if_needed(
      "UV");
  if (uv_id) {
    geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
      Mesh *mesh = geometry_set.get_mesh_for_write();
      if (mesh == nullptr) {
        return;
      }
      Array<float2> uvs(mesh->corners_num);
      geometry::uvw_map(mesh->vert_positions(), mesh->faces(), mesh->corner_verts(), uvw, uvs);
      Array<float3> uvs3(uvs.size());
      for (const int i : uvs.index_range()) {
        uvs3[i] = float3(uvs[i].x, uvs[i].y, 0.0f);
      }
      store_attribute<float3>(*mesh, *uv_id, bke::AttrDomain::Corner, std::move(uvs3));
    });
  }
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Tension
 * \{ */

static void tension_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to measure");
  b.add_output<decl::Float>("Tension").field_on_all().description(
      "Positive where the edges are longer than at rest, negative where they are shorter");
  b.add_input<decl::Vector>("Rest Position")
      .implicit_field_on_all(NODE_DEFAULT_INPUT_POSITION_FIELD)
      .description("Positions of the mesh at rest, for example the \"rest_position\" attribute");
  b.add_input<decl::Float>("Strength").default_value(1.0f).description(
      "Multiply the tension values");
}

static void tension_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<float3> rest_field = params.extract_input<Field<float3>>("Rest Position");
  const float strength = params.extract_input<float>("Strength");
  const std::optional<std::string> id = params.get_output_anonymous_attribute_id_if_needed(
      "Tension");
  if (id) {
    geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
      Mesh *mesh = geometry_set.get_mesh_for_write();
      if (mesh == nullptr) {
        return;
      }
      const bke::MeshFieldContext context{*mesh, bke::AttrDomain::Point};
      fn::FieldEvaluator evaluator{context, mesh->verts_num};
      Array<float3> rest(mesh->verts_num);
      evaluator.add_with_destination(rest_field, rest.as_mutable_span());
      evaluator.evaluate();
      Array<float> tension(mesh->verts_num);
      geometry::mesh_tension(*mesh, rest, strength, tension);
      store_attribute<float>(*mesh, *id, bke::AttrDomain::Point, std::move(tension));
    });
  }
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Radial Array
 * \{ */

static void radial_array_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to copy");
  b.add_input<decl::Int>("Count").default_value(6).min(1).description(
      "Number of copies, including the original");
  b.add_input<decl::Float>("Angle")
      .default_value(2.0f * float(M_PI))
      .subtype(PROP_ANGLE)
      .description("Angle the copies are spread over");
  b.add_input<decl::Menu>("Axis").static_items(axis_items).default_value(MenuValue(2));
  b.add_input<decl::Vector>("Center")
      .subtype(PROP_TRANSLATION)
      .description("Point the axis goes through");
  b.add_input<decl::Float>("Radius").subtype(PROP_DISTANCE).description(
      "Move each copy away from the axis");
  b.add_input<decl::Float>("Step").subtype(PROP_DISTANCE).description(
      "Move each copy along the axis by this much more than the one before");
  b.add_input<decl::Bool>("Rotate Copies").default_value(true).description(
      "Turn the copies to face out from the axis, rather than only moving them");
  b.add_input<decl::Bool>("Merge").default_value(false).description(
      "Merge points of the copies that touch");
  b.add_input<decl::Float>("Merge Distance")
      .default_value(0.001f)
      .min(0.0f)
      .subtype(PROP_DISTANCE);
}

static void radial_array_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  geometry::RadialArrayParams array;
  array.count = params.extract_input<int>("Count");
  array.angle = params.extract_input<float>("Angle");
  array.axis = params.extract_input<MenuValue>("Axis").value;
  array.center = params.extract_input<float3>("Center");
  array.radius = params.extract_input<float>("Radius");
  array.step = params.extract_input<float>("Step");
  array.rotate_copies = params.extract_input<bool>("Rotate Copies");
  array.merge = params.extract_input<bool>("Merge");
  array.merge_distance = params.extract_input<float>("Merge Distance");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *mesh = geometry_set.get_mesh();
    if (mesh == nullptr) {
      return;
    }
    if (Mesh *result = geometry::radial_array(*mesh, array)) {
      geometry::debug_randomize_mesh_order(result);
      geometry_set.replace_mesh(result);
    }
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Volume Select
 * \{ */

static const EnumPropertyItem volume_shape_items[] = {
    {int(geometry::VolumeSelectShape::Sphere), "SPHERE", ICON_NONE, N_("Sphere"), ""},
    {int(geometry::VolumeSelectShape::Box), "BOX", ICON_NONE, N_("Box"), ""},
    {int(geometry::VolumeSelectShape::Cylinder), "CYLINDER", ICON_NONE, N_("Cylinder"), ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static void volume_select_declare(NodeDeclarationBuilder &b)
{
  b.add_output<decl::Float>("Weight").field_source().description(
      "1 inside the shape, fading to 0 over the falloff distance");
  b.add_output<decl::Bool>("Selection").field_source().description(
      "True for the points with a weight above zero");
  b.add_input<decl::Menu>("Shape")
      .static_items(volume_shape_items)
      .default_value(MenuValue(0))
      .optional_label();
  b.add_input<decl::Vector>("Center").subtype(PROP_TRANSLATION).description("Center of the shape");
  b.add_input<decl::Vector>("Rotation").subtype(PROP_EULER).description("Rotation of the shape");
  b.add_input<decl::Vector>("Size")
      .default_value({2.0f, 2.0f, 2.0f})
      .subtype(PROP_XYZ)
      .description("Size of the shape on each axis");
  b.add_input<decl::Float>("Falloff").min(0.0f).subtype(PROP_DISTANCE).description(
      "Distance outside the shape over which the weight fades out");
  b.add_input<decl::Bool>("Invert").default_value(false).description(
      "Select the points outside the shape");
}

class VolumeSelectFunction : public mf::MultiFunction {
 private:
  geometry::VolumeSelector selector_;

 public:
  VolumeSelectFunction(const geometry::VolumeSelector &selector) : selector_(selector)
  {
    static const mf::Signature signature = []() {
      mf::Signature signature;
      mf::SignatureBuilder builder{"Volume Select", signature};
      builder.single_input<float3>("Position");
      builder.single_output<float>("Weight", mf::ParamFlag::SupportsUnusedOutput);
      builder.single_output<bool>("Selection", mf::ParamFlag::SupportsUnusedOutput);
      return signature;
    }();
    this->set_signature(&signature);
  }

  void call(const IndexMask &mask, mf::Params params, mf::Context /*context*/) const override
  {
    const VArraySpan<float3> positions = params.readonly_single_input<float3>(0, "Position");
    MutableSpan<float> weights = params.uninitialized_single_output_if_required<float>(1,
                                                                                        "Weight");
    MutableSpan<bool> selection = params.uninitialized_single_output_if_required<bool>(
        2, "Selection");
    mask.foreach_index([&](const int i) {
      const float weight = selector_.weight(positions[i]);
      if (!weights.is_empty()) {
        weights[i] = weight;
      }
      if (!selection.is_empty()) {
        selection[i] = weight > 0.0f;
      }
    });
  }
};

static void volume_select_exec(GeoNodeExecParams params)
{
  geometry::VolumeSelectParams select;
  select.shape = params.extract_input<geometry::VolumeSelectShape>("Shape");
  select.center = params.extract_input<float3>("Center");
  select.rotation = params.extract_input<float3>("Rotation");
  select.size = params.extract_input<float3>("Size");
  select.falloff = params.extract_input<float>("Falloff");
  select.invert = params.extract_input<bool>("Invert");
  const geometry::VolumeSelector selector(select);

  const Field<float3> position = bke::AttributeFieldInput::from<float3>("position");
  auto fn = std::make_shared<VolumeSelectFunction>(selector);
  const auto operation = FieldOperation::from(std::move(fn), {position});
  params.set_output("Weight", Field<float>(operation, 0));
  params.set_output("Selection", Field<bool>(operation, 1));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Material By Element
 * \{ */

static const EnumPropertyItem material_mode_items[] = {
    {int(geometry::MaterialByElementMode::Random), "RANDOM", ICON_NONE, N_("Random"), N_("A random index for each face")},
    {int(geometry::MaterialByElementMode::Islands), "ISLANDS", ICON_NONE, N_("Islands"), N_("A random index for each connected piece")},
    {int(geometry::MaterialByElementMode::Cycle), "CYCLE", ICON_NONE, N_("Cycle"), N_("Go through the indices in order, face by face")},
    {int(geometry::MaterialByElementMode::Direction), "DIRECTION", ICON_NONE, N_("Direction"), N_("One index for each of the six directions a face can point to")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void material_by_element_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose faces get material indices");
  b.add_input<decl::Bool>("Selection").default_value(true).hide_value().field_on_all();
  b.add_input<decl::Menu>("Mode")
      .static_items(material_mode_items)
      .default_value(MenuValue(0))
      .optional_label();
  b.add_input<decl::Int>("Min Index").default_value(0).min(0);
  b.add_input<decl::Int>("Max Index").default_value(1).min(0);
  b.add_input<decl::Int>("Seed").default_value(0);
}

static void material_by_element_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::MaterialByElementParams material;
  material.mode = params.extract_input<geometry::MaterialByElementMode>("Mode");
  material.min_index = params.extract_input<int>("Min Index");
  material.max_index = params.extract_input<int>("Max Index");
  material.seed = params.extract_input<int>("Seed");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    Mesh *mesh = geometry_set.get_mesh_for_write();
    if (mesh == nullptr || mesh->faces_num == 0) {
      return;
    }
    const bke::MeshFieldContext context{*mesh, bke::AttrDomain::Face};
    fn::FieldEvaluator evaluator{context, mesh->faces_num};
    evaluator.set_selection(selection_field);
    evaluator.evaluate();
    const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
    Array<int> indices(mesh->faces_num);
    geometry::material_by_element(*mesh, material, indices);
    bke::SpanAttributeWriter<int> material_indices =
        mesh->attributes_for_write().lookup_or_add_for_write_span<int>("material_index",
                                                                       bke::AttrDomain::Face);
    selection.foreach_index([&](const int i) { material_indices.span[i] = indices[i]; });
    material_indices.finish();
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Data Channel
 * \{ */

static const EnumPropertyItem channel_source_items[] = {
    {0, "POSITION_X", ICON_NONE, N_("Position X"), ""},
    {1, "POSITION_Y", ICON_NONE, N_("Position Y"), ""},
    {2, "POSITION_Z", ICON_NONE, N_("Position Z"), ""},
    {3, "NORMAL_X", ICON_NONE, N_("Normal X"), ""},
    {4, "NORMAL_Y", ICON_NONE, N_("Normal Y"), ""},
    {5, "NORMAL_Z", ICON_NONE, N_("Normal Z"), ""},
    {6, "CONVEXITY", ICON_NONE, N_("Convexity"), N_("Positive on outward bumps and negative in hollows")},
    {7, "EDGE_LENGTH", ICON_NONE, N_("Edge Length"), N_("Average length of the edges around each point")},
    {8, "FACE_AREA", ICON_NONE, N_("Face Area"), N_("Average area of the faces around each point")},
    {9, "RANDOM", ICON_NONE, N_("Random"), N_("A random value for each point")},
    {10, "DISTANCE_FROM_CENTER", ICON_NONE, N_("Distance from Center"), N_("Distance from the center of the bounding box")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void data_channel_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh to compute the values on");
  b.add_output<decl::Float>("Value").field_on_all().description("The value of each point");
  b.add_input<decl::Menu>("Source")
      .static_items(channel_source_items)
      .default_value(MenuValue(6))
      .optional_label();
  b.add_input<decl::Int>("Seed").default_value(0).usage_by_menu("Source", 9);
  b.add_input<decl::Bool>("Normalize").default_value(true).description(
      "Remap the values so the smallest is 0 and the largest is 1");
  b.add_input<decl::Bool>("Invert").default_value(false);
  b.add_input<decl::Float>("Power").default_value(1.0f).min(0.0f);
  b.add_input<decl::Float>("Multiply").default_value(1.0f);
  b.add_input<decl::Float>("Add").default_value(0.0f);
  b.add_input<decl::Bool>("Clamp").default_value(false);
}

static void data_channel_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  geometry::DataChannelParams channel;
  channel.source = geometry::DataChannelSource(params.extract_input<MenuValue>("Source").value);
  channel.seed = params.extract_input<int>("Seed");
  channel.normalize = params.extract_input<bool>("Normalize");
  channel.invert = params.extract_input<bool>("Invert");
  channel.power = params.extract_input<float>("Power");
  channel.multiply = params.extract_input<float>("Multiply");
  channel.add = params.extract_input<float>("Add");
  channel.clamp = params.extract_input<bool>("Clamp");
  const std::optional<std::string> id = params.get_output_anonymous_attribute_id_if_needed(
      "Value");
  if (id) {
    geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
      Mesh *mesh = geometry_set.get_mesh_for_write();
      if (mesh == nullptr) {
        return;
      }
      Array<float> values(mesh->verts_num);
      geometry::data_channel(*mesh, channel, values);
      store_attribute<float>(*mesh, *id, bke::AttrDomain::Point, std::move(values));
    });
  }
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Face Extrude
 * \{ */

static void face_extrude_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose faces to extrude");
  b.add_output<decl::Bool>("Top").field_on_all().description("The extruded faces");
  b.add_output<decl::Bool>("Side").field_on_all().description("The faces along the sides");
  b.add_input<decl::Bool>("Selection").default_value(true).hide_value().field_on_all();
  b.add_input<decl::Float>("Amount").default_value(0.1f).subtype(PROP_DISTANCE).description(
      "Distance to move the faces along their normals");
  b.add_input<decl::Float>("Scale").default_value(1.0f).min(0.0f).description(
      "Scale of the extruded faces around their center");
  b.add_input<decl::Bool>("Individual").default_value(false).description(
      "Extrude each face on its own, rather than connected faces as one region");
}

static void face_extrude_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::FaceExtrudeParams extrude;
  extrude.amount = params.extract_input<float>("Amount");
  extrude.scale = params.extract_input<float>("Scale");
  extrude.individual = params.extract_input<bool>("Individual");
  extrude.top_id = params.get_output_anonymous_attribute_id_if_needed("Top");
  extrude.side_id = params.get_output_anonymous_attribute_id_if_needed("Side");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *mesh = geometry_set.get_mesh();
    if (mesh == nullptr || mesh->faces_num == 0) {
      return;
    }
    const bke::MeshFieldContext context{*mesh, bke::AttrDomain::Face};
    fn::FieldEvaluator evaluator{context, mesh->faces_num};
    evaluator.set_selection(selection_field);
    evaluator.evaluate();
    const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
    if (Mesh *result = geometry::mesh_face_extrude(*mesh, selection, extrude)) {
      geometry::debug_randomize_mesh_order(result);
      geometry_set.replace_mesh(result);
    }
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

/* -------------------------------------------------------------------- */
/** \name Quadrangulate
 * \{ */

static void quadrangulate_declare(NodeDeclarationBuilder &b)
{
  declare_mesh(b, "Mesh whose triangles to join into quads");
  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Triangles that can be joined");
  b.add_input<decl::Float>("Max Face Angle")
      .default_value(DEG2RADF(40.0f))
      .min(0.0f)
      .max(float(M_PI))
      .subtype(PROP_ANGLE)
      .description("Largest angle between two triangles that can be joined");
  b.add_input<decl::Float>("Max Shape Angle")
      .default_value(DEG2RADF(40.0f))
      .min(0.0f)
      .max(float(M_PI))
      .subtype(PROP_ANGLE)
      .description("Largest difference from a square corner of the joined quad");
  b.add_input<decl::Float>("Topology Influence")
      .default_value(0.0f)
      .min(0.0f)
      .max(2.0f)
      .subtype(PROP_FACTOR)
      .description(
          "How much to prioritize regular grids of quads as well as quads that touch existing "
          "quads");
  b.add_input<decl::Bool>("Compare UVs").description("Don't join triangles across a UV seam");
  b.add_input<decl::Bool>("Compare Color Attributes")
      .description("Don't join triangles with different colors along their edge");
  b.add_input<decl::Bool>("Compare Seam").description("Don't join triangles across a seam");
  b.add_input<decl::Bool>("Compare Sharp").description("Don't join triangles across a sharp edge");
  b.add_input<decl::Bool>("Compare Materials")
      .description("Don't join triangles with different materials");
}

static void quadrangulate_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  geometry::QuadrangulateParams quad;
  quad.face_threshold = params.extract_input<float>("Max Face Angle");
  quad.shape_threshold = params.extract_input<float>("Max Shape Angle");
  quad.topology_influence = params.extract_input<float>("Topology Influence");
  quad.compare_uvs = params.extract_input<bool>("Compare UVs");
  quad.compare_vcols = params.extract_input<bool>("Compare Color Attributes");
  quad.compare_seam = params.extract_input<bool>("Compare Seam");
  quad.compare_sharp = params.extract_input<bool>("Compare Sharp");
  quad.compare_materials = params.extract_input<bool>("Compare Materials");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *mesh = geometry_set.get_mesh();
    if (mesh == nullptr || mesh->faces_num == 0) {
      return;
    }
    const bke::MeshFieldContext context{*mesh, bke::AttrDomain::Face};
    fn::FieldEvaluator evaluator{context, mesh->faces_num};
    evaluator.set_selection(selection_field);
    evaluator.evaluate();
    const IndexMask selection = evaluator.get_evaluated_selection_as_mask();
    if (Mesh *result = geometry::mesh_quadrangulate(*mesh, selection, quad)) {
      geometry::debug_randomize_mesh_order(result);
      geometry_set.replace_mesh(result);
    }
  });
  params.set_output("Mesh", std::move(geometry_set));
}

/** \} */

static void node_register()
{
  static blender::bke::bNodeType cap_holes, uvw_map, tension, radial_array, volume_select,
      material_by_element, data_channel, face_extrude, quadrangulate;
  register_node(cap_holes,
                "GeometryNodeCapHoles",
                "Cap Holes",
                "Close the holes of a mesh with new faces",
                cap_holes_declare,
                cap_holes_exec);
  register_node(uvw_map,
                "GeometryNodeUVWMap",
                "UVW Map",
                "Project a UV map from a planar, cylindrical, spherical or box gizmo",
                uvw_map_declare,
                uvw_map_exec);
  register_node(tension,
                "GeometryNodeMeshTension",
                "Mesh Tension",
                "How much the edges around each point are stretched or squashed compared to a "
                "rest shape",
                tension_declare,
                tension_exec);
  register_node(radial_array,
                "GeometryNodeRadialArray",
                "Radial Array",
                "Copies of a mesh arranged around an axis",
                radial_array_declare,
                radial_array_exec);
  register_node(volume_select,
                "GeometryNodeVolumeSelect",
                "Volume Select",
                "Select the points inside a sphere, box or cylinder, with a soft falloff",
                volume_select_declare,
                volume_select_exec);
  volume_select.nclass = NODE_CLASS_INPUT;
  register_node(material_by_element,
                "GeometryNodeMaterialByElement",
                "Material By Element",
                "Give faces material indices at random, by piece, in order or by direction",
                material_by_element_declare,
                material_by_element_exec);
  register_node(data_channel,
                "GeometryNodeDataChannel",
                "Data Channel",
                "Compute a value per point, such as convexity or edge length, and remap it",
                data_channel_declare,
                data_channel_exec);
  register_node(face_extrude,
                "GeometryNodeFaceExtrude",
                "Face Extrude",
                "Extrude faces along their normals and scale them",
                face_extrude_declare,
                face_extrude_exec);
  register_node(quadrangulate,
                "GeometryNodeQuadrangulate",
                "Quadrangulate",
                "Join pairs of triangles into quads, the same as Triangles to Quads in edit mode",
                quadrangulate_declare,
                quadrangulate_exec);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_mesh_modifier_ops_cc

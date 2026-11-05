/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * Bend, Twist, Taper and Stretch nodes. The same code as the modifiers of the same names, see
 * #geometry::axis_deform. They work on the points of any geometry.
 */

#include "BKE_attribute.hh"
#include "BKE_curves.hh"
#include "BKE_geometry_fields.hh"

#include "GEO_axis_deform.hh"
#include "GEO_foreach_geometry.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_axis_deform_cc {

using geometry::AxisDeformType;

static const EnumPropertyItem axis_items[] = {
    {0, "X", ICON_NONE, N_("X"), ""},
    {1, "Y", ICON_NONE, N_("Y"), ""},
    {2, "Z", ICON_NONE, N_("Z"), ""},
    {0, nullptr, 0, nullptr, nullptr},
};

template<AxisDeformType Type> static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();
  b.add_input<decl::Geometry>("Geometry").is_default_link_socket().description(
      "Geometry whose points to move");
  b.add_output<decl::Geometry>("Geometry").propagate_all().align_with_previous();
  b.add_input<decl::Float>("Weight")
      .default_value(1.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .field_on_all()
      .description("How much each point is affected");

  if constexpr (ELEM(Type, AxisDeformType::Bend, AxisDeformType::Twist)) {
    b.add_input<decl::Float>("Angle")
        .default_value(float(M_PI_2))
        .subtype(PROP_ANGLE)
        .description("Total angle over the length of the shape or between the limits");
  }
  if constexpr (Type == AxisDeformType::Bend) {
    b.add_input<decl::Float>("Direction").subtype(PROP_ANGLE).description(
        "Side to bend toward, around the axis");
  }
  if constexpr (Type == AxisDeformType::Taper) {
    b.add_input<decl::Float>("Amount").default_value(0.5f).description(
        "How much bigger the top is than the center");
    b.add_input<decl::Float>("Curve").description("Bulge of the sides");
  }
  if constexpr (Type == AxisDeformType::Stretch) {
    b.add_input<decl::Float>("Stretch").default_value(0.5f).min(-0.99f).description(
        "How much longer the shape gets along the axis");
    b.add_input<decl::Float>("Amplify").description(
        "How much thinner the middle gets as it stretches");
  }

  b.add_input<decl::Menu>("Axis").static_items(axis_items).default_value(MenuValue(2));
  b.add_input<decl::Vector>("Center").subtype(PROP_TRANSLATION).description(
      "Center of the gizmo");
  b.add_input<decl::Vector>("Rotation").subtype(PROP_EULER).description("Rotation of the gizmo");
  b.add_input<decl::Bool>("Limit Effect").default_value(false).description(
      "Only change the part between the limits, and carry the rest along");
  b.add_input<decl::Float>("Lower Limit")
      .default_value(-1.0f)
      .subtype(PROP_DISTANCE);
  b.add_input<decl::Float>("Upper Limit")
      .default_value(1.0f)
      .subtype(PROP_DISTANCE);
}

static void deform_component(GeometryComponent &component,
                             const geometry::AxisDeformParams &deform,
                             const Field<float> &weight_field)
{
  const int domain_size = component.attribute_domain_size(bke::AttrDomain::Point);
  if (domain_size == 0) {
    return;
  }
  const bke::GeometryFieldContext context{component, bke::AttrDomain::Point};
  fn::FieldEvaluator evaluator{context, domain_size};
  Array<float> weights(domain_size);
  evaluator.add_with_destination(weight_field, weights.as_mutable_span());
  evaluator.evaluate();

  bke::MutableAttributeAccessor attributes = *component.attributes_for_write();
  bke::SpanAttributeWriter<float3> positions = attributes.lookup_for_write_span<float3>(
      "position");
  if (!positions) {
    return;
  }
  geometry::axis_deform(positions.span, deform, weights);
  positions.finish();
}

template<AxisDeformType Type> static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Geometry");
  const Field<float> weight_field = params.extract_input<Field<float>>("Weight");

  geometry::AxisDeformParams deform;
  deform.type = Type;
  if constexpr (ELEM(Type, AxisDeformType::Bend, AxisDeformType::Twist)) {
    deform.angle = params.extract_input<float>("Angle");
  }
  if constexpr (Type == AxisDeformType::Bend) {
    deform.direction = params.extract_input<float>("Direction");
  }
  if constexpr (Type == AxisDeformType::Taper) {
    deform.amount = params.extract_input<float>("Amount");
    deform.curve = params.extract_input<float>("Curve");
  }
  if constexpr (Type == AxisDeformType::Stretch) {
    deform.amount = params.extract_input<float>("Stretch");
    deform.curve = params.extract_input<float>("Amplify");
  }
  deform.axis = params.extract_input<MenuValue>("Axis").value;
  deform.center = params.extract_input<float3>("Center");
  deform.rotation = params.extract_input<float3>("Rotation");
  deform.use_limits = params.extract_input<bool>("Limit Effect");
  deform.lower = params.extract_input<float>("Lower Limit");
  deform.upper = params.extract_input<float>("Upper Limit");

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    for (const GeometryComponent::Type type : {GeometryComponent::Type::Mesh,
                                               GeometryComponent::Type::PointCloud,
                                               GeometryComponent::Type::Curve})
    {
      if (geometry_set.has(type)) {
        deform_component(geometry_set.get_component_for_write(type), deform, weight_field);
      }
    }
  });
  params.set_output("Geometry", std::move(geometry_set));
}

template<AxisDeformType Type>
static void register_one(const char *idname, const char *ui_name, const char *description)
{
  static blender::bke::bNodeType ntype;
  geo_node_type_base(&ntype, idname);
  ntype.ui_name = ui_name;
  ntype.ui_description = description;
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare<Type>;
  ntype.geometry_node_execute = node_geo_exec<Type>;
  blender::bke::node_register_type(ntype);
}

static void node_register()
{
  register_one<AxisDeformType::Bend>(
      "GeometryNodeBend", "Bend", "Bend geometry around a gizmo, with limits");
  register_one<AxisDeformType::Twist>(
      "GeometryNodeTwist", "Twist", "Twist geometry around the axis of a gizmo, with limits");
  register_one<AxisDeformType::Taper>(
      "GeometryNodeTaper", "Taper", "Scale geometry more along the axis of a gizmo");
  register_one<AxisDeformType::Stretch>(
      "GeometryNodeStretch",
      "Stretch",
      "Stretch geometry along the axis of a gizmo and thin it in the middle");
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_axis_deform_cc

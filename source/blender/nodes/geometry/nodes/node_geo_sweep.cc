/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "DNA_mesh_types.h"

#include "BLI_math_rotation.h"
#include "BLI_math_vector.hh"

#include "BKE_attribute.hh"
#include "BKE_curve_to_mesh.hh"
#include "BKE_curves.hh"
#include "BKE_instances.hh"
#include "BKE_mesh.hh"

#include "GEO_curve_sweep.hh"
#include "GEO_foreach_geometry.hh"
#include "GEO_randomize.hh"

#include "NOD_rna_define.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_sweep_cc {

enum class Shape {
  RoundTube = 0,
  SquareTube = 1,
  Ribbon = 2,
  CrossSection = 3,
};

static const EnumPropertyItem shape_items[] = {
    {int(Shape::RoundTube), "ROUND_TUBE", ICON_NONE, N_("Round Tube"), N_("Sweep a circle")},
    {int(Shape::SquareTube), "SQUARE_TUBE", ICON_NONE, N_("Square Tube"), N_("Sweep a square")},
    {int(Shape::Ribbon), "RIBBON", ICON_NONE, N_("Ribbon"), N_("Sweep a straight line")},
    {int(Shape::CrossSection),
     "CROSS_SECTION",
     ICON_NONE,
     N_("Cross Section"),
     N_("Sweep the curves of the Cross Section input")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Curve")
      .supported_type(GeometryComponent::Type::Curve)
      .is_default_link_socket()
      .description("Curves to sweep the shape along");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Vector>("UV Map")
      .field_on_all()
      .description("UV coordinates: X goes around the shape and Y goes along the curve");
  b.add_output<decl::Bool>("Caps").field_on_all().description("The faces that close the ends");

  b.add_input<decl::Menu>("Shape")
      .static_items(shape_items)
      .default_value(Shape::RoundTube)
      .optional_label();
  b.add_input<decl::Geometry>("Cross Section")
      .only_realized_data()
      .supported_type(GeometryComponent::Type::Curve)
      .usage_by_single_menu(int(Shape::CrossSection))
      .description("Curves to sweep along every curve of the main input");
  b.add_input<decl::Int>("Columns")
      .default_value(12)
      .min(1)
      .max(10000)
      .usage_by_menu("Shape", {int(Shape::RoundTube), int(Shape::SquareTube), int(Shape::Ribbon)})
      .description("Number of faces around the shape");
  b.add_input<decl::Float>("Radius")
      .default_value(0.1f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_single_menu(int(Shape::RoundTube));
  b.add_input<decl::Float>("Width")
      .default_value(0.2f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Shape", {int(Shape::SquareTube), int(Shape::Ribbon)});
  b.add_input<decl::Float>("Scale")
      .default_value(1.0f)
      .min(0.0f)
      .field_on({0})
      .description("Scale of the shape at each point of the curve");
  b.add_input<decl::Float>("Roll")
      .default_value(0.0f)
      .subtype(PROP_ANGLE)
      .field_on({0})
      .description("Rotation of the shape around the curve at each point");
  b.add_input<decl::Float>("Twist")
      .default_value(0.0f)
      .subtype(PROP_ANGLE)
      .field_on({0})
      .description(
          "Rotation of the shape that is reached at the end of the curve, growing from none at "
          "its start. The curve needs enough points to show it");
  b.add_input<decl::Bool>("Fill Caps")
      .default_value(false)
      .description("Close the ends of curves that aren't cyclic, if the shape is closed");
  b.add_input<decl::Bool>("Length Based UVs")
      .default_value(true)
      .description(
          "Repeat the UV map along the curve so that faces keep their proportions, instead of "
          "fitting it into the length of each curve");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet curve_set = params.extract_input<GeometrySet>("Curve");
  const GeometrySet cross_section_set = params.extract_input<GeometrySet>("Cross Section");
  const Shape shape = params.extract_input<Shape>("Shape");
  const int columns = params.extract_input<int>("Columns");
  const float radius = params.extract_input<float>("Radius");
  const float width = params.extract_input<float>("Width");
  const Field<float> scale_field = params.extract_input<Field<float>>("Scale");
  const Field<float> roll_field = params.extract_input<Field<float>>("Roll");
  const Field<float> twist_field = params.extract_input<Field<float>>("Twist");
  const bool fill_caps = params.extract_input<bool>("Fill Caps");
  const bool length_based = params.extract_input<bool>("Length Based UVs");
  const std::optional<std::string> uv_id = params.get_output_anonymous_attribute_id_if_needed(
      "UV Map");
  const std::optional<std::string> caps_id = params.get_output_anonymous_attribute_id_if_needed(
      "Caps");

  bke::CurvesGeometry built_profile;
  const bke::CurvesGeometry *profile = &built_profile;
  if (shape == Shape::CrossSection) {
    if (const Curves *curves = cross_section_set.get_curves()) {
      profile = &curves->geometry.wrap();
    }
    else {
      params.error_message_add(NodeWarningType::Info, TIP_("There is no cross section curve"));
      params.set_output("Mesh", GeometrySet());
      return;
    }
  }
  else {
    built_profile = geometry::sweep_shape_profile(
        geometry::SweepShape(shape), columns, radius, width);
  }

  bke::GeometryComponentEditData::remember_deformed_positions_if_necessary(curve_set);
  const AttributeFilter &attribute_filter = params.get_attribute_filter("Mesh");

  geometry::foreach_real_geometry(curve_set, [&](GeometrySet &geometry_set) {
    if (const Curves *curves_id = geometry_set.get_curves()) {
      bke::CurvesGeometry main = curves_id->geometry.wrap();

      const bke::CurvesFieldContext context(*curves_id, bke::AttrDomain::Point);
      FieldEvaluator evaluator(context, main.points_num());
      evaluator.add(scale_field);
      evaluator.add(roll_field);
      evaluator.add(twist_field);
      evaluator.evaluate();

      geometry::sweep_add_roll_and_twist(
          main, evaluator.get_evaluated<float>(1), evaluator.get_evaluated<float>(2));

      geometry::SweepOutputs outputs;
      outputs.uv_map = uv_id;
      outputs.caps = caps_id;
      Mesh *mesh = geometry::curve_sweep(main,
                                         *profile,
                                         evaluator.get_evaluated<float>(0),
                                         fill_caps,
                                         length_based,
                                         outputs,
                                         attribute_filter);
      if (mesh != nullptr) {
        mesh->mat = MEM_dupalloc(curves_id->mat);
        mesh->totcol = curves_id->totcol;
        geometry::debug_randomize_mesh_order(mesh);
      }
      geometry_set.replace_mesh(mesh);
    }
    geometry_set.keep_only({GeometryComponent::Type::Mesh,
                            GeometryComponent::Type::Instance,
                            GeometryComponent::Type::Edit});
  });

  params.set_output("Mesh", std::move(curve_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeSweep");
  ntype.ui_name = "Sweep";
  ntype.ui_description =
      "Make a mesh by moving a shape along curves, with control over its scale, roll and twist";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_sweep_cc

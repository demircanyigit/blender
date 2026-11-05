/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup nodes
 *
 * Morph Curves and Relax Curves nodes, the same code as the Spline Morph and Relax Spline
 * modifiers, see GEO_curve_modifiers.hh.
 */

#include "BKE_curves.hh"

#include "GEO_curve_modifiers.hh"
#include "GEO_foreach_geometry.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_curve_modifier_ops_cc {

static void morph_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();
  b.add_input<decl::Geometry>("Curves")
      .supported_type(GeometryComponent::Type::Curve)
      .is_default_link_socket()
      .description("Curves to move");
  b.add_output<decl::Geometry>("Curves").propagate_all().align_with_previous();
  b.add_input<decl::Geometry>("Target")
      .supported_type(GeometryComponent::Type::Curve)
      .description("Curves to morph toward. Each curve moves toward the curve of the same index");
  b.add_input<decl::Float>("Factor").default_value(0.5f).subtype(PROP_FACTOR).description(
      "How far to move toward the target");
}

static void morph_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Curves");
  const GeometrySet target_set = params.extract_input<GeometrySet>("Target");
  const float factor = params.extract_input<float>("Factor");
  const Curves *target = target_set.get_curves();
  if (target == nullptr) {
    params.set_output("Curves", std::move(geometry_set));
    return;
  }
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    if (Curves *curves = geometry_set.get_curves_for_write()) {
      geometry::morph_curves(curves->geometry.wrap(), target->geometry.wrap(), factor);
    }
  });
  params.set_output("Curves", std::move(geometry_set));
}

static void relax_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();
  b.add_input<decl::Geometry>("Curves")
      .supported_type(GeometryComponent::Type::Curve)
      .is_default_link_socket()
      .description("Curves to smooth");
  b.add_output<decl::Geometry>("Curves").propagate_all().align_with_previous();
  b.add_input<decl::Int>("Iterations").default_value(5).min(0).description(
      "Number of smoothing steps");
  b.add_input<decl::Float>("Factor").default_value(0.5f).subtype(PROP_FACTOR).description(
      "How much to smooth in each step");
  b.add_input<decl::Bool>("Keep Ends").default_value(true).description(
      "Keep the ends of the curves in place");
  b.add_input<decl::Bool>("Keep Shape").default_value(false).description(
      "Try to keep the overall shape");
}

static void relax_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Curves");
  const int iterations = params.extract_input<int>("Iterations");
  const float factor = params.extract_input<float>("Factor");
  const bool keep_ends = params.extract_input<bool>("Keep Ends");
  const bool keep_shape = params.extract_input<bool>("Keep Shape");
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    if (Curves *curves = geometry_set.get_curves_for_write()) {
      geometry::relax_curves(curves->geometry.wrap(), iterations, factor, keep_ends, keep_shape);
    }
  });
  params.set_output("Curves", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType morph;
  geo_node_type_base(&morph, "GeometryNodeMorphCurves");
  morph.ui_name = "Morph Curves";
  morph.ui_description = "Move curves toward the matching curves of a target";
  morph.nclass = NODE_CLASS_GEOMETRY;
  morph.declare = morph_declare;
  morph.geometry_node_execute = morph_exec;
  blender::bke::node_register_type(morph);

  static blender::bke::bNodeType relax;
  geo_node_type_base(&relax, "GeometryNodeRelaxCurves");
  relax.ui_name = "Relax Curves";
  relax.ui_description = "Smooth the shape of curves";
  relax.nclass = NODE_CLASS_GEOMETRY;
  relax.declare = relax_declare;
  relax.geometry_node_execute = relax_exec;
  blender::bke::node_register_type(relax);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_curve_modifier_ops_cc

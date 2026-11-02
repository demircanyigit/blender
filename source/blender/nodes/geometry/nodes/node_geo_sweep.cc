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

static bke::CurvesGeometry make_poly_profile(const int points_num, const bool cyclic)
{
  bke::CurvesGeometry profile(points_num, 1);
  MutableSpan<int> offsets = profile.offsets_for_write();
  offsets.first() = 0;
  offsets.last() = points_num;
  profile.fill_curve_types(CURVE_TYPE_POLY);
  profile.cyclic_for_write().first() = cyclic;
  return profile;
}

static bke::CurvesGeometry make_round_tube(const int columns, const float radius)
{
  const int points_num = std::max(columns, 3);
  bke::CurvesGeometry profile = make_poly_profile(points_num, true);
  MutableSpan<float3> positions = profile.positions_for_write();
  for (const int i : positions.index_range()) {
    const float angle = float(M_PI) * 2.0f * float(i) / float(points_num);
    positions[i] = float3(std::cos(angle), std::sin(angle), 0.0f) * radius;
  }
  return profile;
}

static bke::CurvesGeometry make_square_tube(const int columns, const float width)
{
  const int side_segments = std::max((columns + 2) / 4, 1);
  bke::CurvesGeometry profile = make_poly_profile(side_segments * 4, true);
  MutableSpan<float3> positions = profile.positions_for_write();
  const float half = width / 2.0f;
  const std::array<float2, 5> corners = {
      float2(-half, -half), float2(half, -half), float2(half, half), float2(-half, half),
      float2(-half, -half)};
  for (const int side : IndexRange(4)) {
    for (const int i : IndexRange(side_segments)) {
      const float2 position = math::interpolate(
          corners[side], corners[side + 1], float(i) / float(side_segments));
      positions[side * side_segments + i] = float3(position, 0.0f);
    }
  }

  /* Only the corners are sharp, so that the sides can stay smooth along a bent curve. */
  bke::SpanAttributeWriter<bool> sharp_edges =
      profile.attributes_for_write().lookup_or_add_for_write_span<bool>("sharp_edge",
                                                                        bke::AttrDomain::Point);
  for (const int side : IndexRange(4)) {
    sharp_edges.span[side * side_segments] = true;
  }
  sharp_edges.finish();
  return profile;
}

static bke::CurvesGeometry make_ribbon(const int columns, const float width)
{
  const int segments = std::max(columns, 1);
  bke::CurvesGeometry profile = make_poly_profile(segments + 1, false);
  MutableSpan<float3> positions = profile.positions_for_write();
  for (const int i : positions.index_range()) {
    positions[i] = float3((float(i) / float(segments) - 0.5f) * width, 0.0f, 0.0f);
  }
  return profile;
}

/** The sweep gets its rotation from the tilt of the curve. */
static void add_roll_and_twist(bke::CurvesGeometry &curves,
                               const VArray<float> &roll,
                               const VArray<float> &twist)
{
  if (roll.get_if_single() == 0.0f && twist.get_if_single() == 0.0f) {
    return;
  }
  const OffsetIndices<int> points_by_curve = curves.points_by_curve();
  const Span<float3> positions = curves.positions();
  const VArray<bool> cyclic = curves.cyclic();
  MutableSpan<float> tilts = curves.tilt_for_write();
  threading::parallel_for(curves.curves_range(), 256, [&](const IndexRange range) {
    Vector<float> lengths;
    for (const int curve : range) {
      const IndexRange points = points_by_curve[curve];
      lengths.resize(points.size());
      float total = 0.0f;
      for (const int i : points.index_range()) {
        lengths[i] = total;
        const int next = (i + 1 < points.size()) ? i + 1 : (cyclic[curve] ? 0 : i);
        total += math::distance(positions[points[i]], positions[points[next]]);
      }
      for (const int i : points.index_range()) {
        const int point = points[i];
        const float factor = total > 0.0f ? lengths[i] / total : 0.0f;
        tilts[point] += roll[point] + twist[point] * factor;
      }
    }
  });
  curves.tag_normals_changed();
}

/** How far along the points each segment starts, plus the end of the last one. */
static Array<float> calc_accumulated_lengths(const Span<float3> positions, const int segments_num)
{
  Array<float> lengths(segments_num + 1);
  lengths.first() = 0.0f;
  for (const int i : IndexRange(segments_num)) {
    lengths[i + 1] = lengths[i] + math::distance(positions[i],
                                                 positions[(i + 1) % positions.size()]);
  }
  return lengths;
}

/** The number of quads along a curve, the same as #bke::curve_to_mesh_sweep makes them. */
static int main_segments_num(const int points_num, const bool cyclic)
{
  if (points_num == 0) {
    return 0;
  }
  return bke::curves::segments_num(points_num, points_num > 2 && cyclic);
}

/**
 * Fill the attributes that need to know which corner came from which point of the two curves. This
 * follows the order of the corners of #bke::curve_to_mesh_sweep.
 */
static void fill_sweep_attributes(const bke::CurvesGeometry &main,
                                  const bke::CurvesGeometry &profile,
                                  const bool fill_caps,
                                  const bool length_based,
                                  const std::optional<std::string> &uv_id,
                                  const std::optional<std::string> &caps_id,
                                  Mesh &mesh)
{
  if (!uv_id && !caps_id) {
    return;
  }
  bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
  bke::SpanAttributeWriter<float2> uvs;
  bke::SpanAttributeWriter<bool> caps;
  if (uv_id) {
    uvs = attributes.lookup_or_add_for_write_only_span<float2>(*uv_id, bke::AttrDomain::Corner);
  }
  if (caps_id) {
    caps = attributes.lookup_or_add_for_write_span<bool>(*caps_id, bke::AttrDomain::Face);
  }

  const OffsetIndices<int> main_points_by_curve = main.evaluated_points_by_curve();
  const OffsetIndices<int> profile_points_by_curve = profile.evaluated_points_by_curve();
  const Span<float3> main_positions = main.evaluated_positions();
  const Span<float3> profile_positions = profile.evaluated_positions();
  const VArraySpan<bool> main_cyclic = main.cyclic();
  const VArraySpan<bool> profile_cyclic = profile.cyclic();

  int corner_offset = 0;
  int face_offset = 0;
  for (const int i_main : main_points_by_curve.index_range()) {
    const IndexRange main_points = main_points_by_curve[i_main];
    const int main_segments = main_segments_num(main_points.size(), main_cyclic[i_main]);
    const Array<float> main_lengths = calc_accumulated_lengths(main_positions.slice(main_points),
                                                               main_segments);
    for (const int i_profile : profile_points_by_curve.index_range()) {
      const IndexRange profile_points = profile_points_by_curve[i_profile];
      if (profile_points.is_empty()) {
        continue;
      }
      const Span<float3> positions = profile_positions.slice(profile_points);
      const bool cyclic = profile_cyclic[i_profile];
      const int profile_segments = bke::curves::segments_num(profile_points.size(), cyclic);
      const Array<float> profile_lengths = calc_accumulated_lengths(positions, profile_segments);
      const int tube_faces = main_segments * profile_segments;
      const bool has_caps = fill_caps && !main_cyclic[i_main] && cyclic && profile_segments > 2;

      if (uvs) {
        const float profile_total = profile_lengths.last();
        const float main_total = main_lengths.last();
        const float u_scale = profile_total > 0.0f ? 1.0f / profile_total : 0.0f;
        const float v_divisor = (length_based && profile_total > 0.0f) ? profile_total :
                                                                         main_total;
        const float v_scale = v_divisor > 0.0f ? 1.0f / v_divisor : 0.0f;
        for (const int ring : IndexRange(main_segments)) {
          const float v = main_lengths[ring] * v_scale;
          const float v_next = main_lengths[ring + 1] * v_scale;
          for (const int i : IndexRange(profile_segments)) {
            const float u = profile_lengths[i] * u_scale;
            const float u_next = profile_lengths[i + 1] * u_scale;
            MutableSpan<float2> quad = uvs.span.slice(
                corner_offset + (ring * profile_segments + i) * 4, 4);
            quad[0] = float2(u, v);
            quad[1] = float2(u_next, v);
            quad[2] = float2(u_next, v_next);
            quad[3] = float2(u, v_next);
          }
        }
        if (has_caps) {
          /* Fit the shape into the UV square. */
          float3 min(FLT_MAX), max(-FLT_MAX);
          for (const float3 &position : positions) {
            math::min_max(position, min, max);
          }
          const float size = std::max(max.x - min.x, max.y - min.y);
          const float scale = size > 0.0f ? 1.0f / size : 0.0f;
          const int cap_start = corner_offset + tube_faces * 4;
          for (const int i : IndexRange(profile_segments)) {
            const float2 first = (positions[profile_segments - i - 1].xy() - min.xy()) * scale;
            const float2 last = (positions[i].xy() - min.xy()) * scale;
            uvs.span[cap_start + i] = first;
            uvs.span[cap_start + profile_segments + i] = last;
          }
        }
      }
      if (caps && has_caps) {
        caps.span[face_offset + tube_faces] = true;
        caps.span[face_offset + tube_faces + 1] = true;
      }

      corner_offset += tube_faces * 4 + (has_caps ? profile_segments * 2 : 0);
      face_offset += tube_faces + (has_caps ? 2 : 0);
    }
  }

  uvs.finish();
  caps.finish();
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
  switch (shape) {
    case Shape::RoundTube:
      built_profile = make_round_tube(columns, radius);
      break;
    case Shape::SquareTube:
      built_profile = make_square_tube(columns, width);
      break;
    case Shape::Ribbon:
      built_profile = make_ribbon(columns, width);
      break;
    case Shape::CrossSection:
      if (const Curves *curves = cross_section_set.get_curves()) {
        profile = &curves->geometry.wrap();
      }
      else {
        params.error_message_add(NodeWarningType::Info, TIP_("There is no cross section curve"));
        params.set_output("Mesh", GeometrySet());
        return;
      }
      break;
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

      add_roll_and_twist(
          main, evaluator.get_evaluated<float>(1), evaluator.get_evaluated<float>(2));

      Mesh *mesh = bke::curve_to_mesh_sweep(
          main, *profile, evaluator.get_evaluated<float>(0), fill_caps, attribute_filter);
      if (mesh != nullptr) {
        fill_sweep_attributes(main, *profile, fill_caps, length_based, uv_id, caps_id, *mesh);
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

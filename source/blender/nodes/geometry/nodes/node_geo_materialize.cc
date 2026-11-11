/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BLI_math_base.h"
#include "DNA_mesh_types.h"

#include "BKE_material.hh"
#include "BKE_mesh.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_materialize.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_materialize_cc {

using geometry::MaterializeCurvature;
using geometry::MaterializeDirection;
using geometry::MaterializeRandom;
using geometry::MaterializeType;

static const EnumPropertyItem type_items[] = {
    {int(MaterializeType::Curvature),
     "CURVATURE",
     ICON_NONE,
     N_("Curvature"),
     N_("Faces on sharp edges or in creases")},
    {int(MaterializeType::Direction),
     "DIRECTION",
     ICON_NONE,
     N_("Direction"),
     N_("Faces facing a way")},
    {int(MaterializeType::Height), "HEIGHT", ICON_NONE, N_("Height"), N_("Faces in a band of height")},
    {int(MaterializeType::Slope),
     "SLOPE",
     ICON_NONE,
     N_("Slope"),
     N_("Faces whose slope is in a range")},
    {int(MaterializeType::Noise), "NOISE", ICON_NONE, N_("Noise"), N_("Faces in patches of noise")},
    {int(MaterializeType::Random), "RANDOM", ICON_NONE, N_("Random"), N_("Faces picked at random")},
    {int(MaterializeType::Attribute),
     "VALUE",
     ICON_NONE,
     N_("Value"),
     N_("Faces where a value is above a threshold")},
    {int(MaterializeType::Proximity),
     "PROXIMITY",
     ICON_NONE,
     N_("Proximity"),
     N_("Faces near another mesh")},
    {int(MaterializeType::Occlusion),
     "OCCLUSION",
     ICON_NONE,
     N_("Ambient Occlusion"),
     N_("Faces in hidden, shadowed places")},
    {int(MaterializeType::Thickness),
     "THICKNESS",
     ICON_NONE,
     N_("Thickness"),
     N_("Faces where the mesh is thin")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem curvature_items[] = {
    {int(MaterializeCurvature::Convex), "CONVEX", ICON_NONE, N_("Convex"), N_("Sharp edges")},
    {int(MaterializeCurvature::Concave), "CONCAVE", ICON_NONE, N_("Concave"), N_("Creases")},
    {int(MaterializeCurvature::Both), "BOTH", ICON_NONE, N_("Both"), N_("Edges and creases")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem direction_items[] = {
    {int(MaterializeDirection::Up), "UP", ICON_NONE, N_("Up"), ""},
    {int(MaterializeDirection::Down), "DOWN", ICON_NONE, N_("Down"), ""},
    {int(MaterializeDirection::Sides), "SIDES", ICON_NONE, N_("Sides"), N_("Walls")},
    {int(MaterializeDirection::PosX), "POS_X", ICON_NONE, N_("+X"), ""},
    {int(MaterializeDirection::NegX), "NEG_X", ICON_NONE, N_("-X"), ""},
    {int(MaterializeDirection::PosY), "POS_Y", ICON_NONE, N_("+Y"), ""},
    {int(MaterializeDirection::NegY), "NEG_Y", ICON_NONE, N_("-Y"), ""},
    {int(MaterializeDirection::Custom), "CUSTOM", ICON_NONE, N_("Custom"), ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem axis_items[] = {
    {0, "X", ICON_NONE, N_("X"), ""},
    {1, "Y", ICON_NONE, N_("Y"), ""},
    {2, "Z", ICON_NONE, N_("Z"), ""},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem random_items[] = {
    {int(MaterializeRandom::Face), "FACE", ICON_NONE, N_("Face"), N_("Each face on its own")},
    {int(MaterializeRandom::Island),
     "ISLAND",
     ICON_NONE,
     N_("Island"),
     N_("Each loose part as a whole")},
    {int(MaterializeRandom::Object), "ALL", ICON_NONE, N_("All"), N_("The whole mesh or none")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh to give the material to");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_output<decl::Bool>("Picked").field_on_all().description(
      "Faces that got the material");

  b.add_input<decl::Bool>("Selection")
      .default_value(true)
      .hide_value()
      .field_on_all()
      .description("Faces that may get the material");
  b.add_input<decl::Material>("Material").optional_label();
  b.add_input<decl::Menu>("Type")
      .static_items(type_items)
      .default_value(MaterializeType::Direction)
      .optional_label()
      .description("What picks the faces");

  const int curvature = int(MaterializeType::Curvature);
  const int direction = int(MaterializeType::Direction);
  const int height = int(MaterializeType::Height);
  const int slope = int(MaterializeType::Slope);
  const int noise = int(MaterializeType::Noise);
  const int random = int(MaterializeType::Random);
  const int value = int(MaterializeType::Attribute);
  const int proximity = int(MaterializeType::Proximity);
  const int occlusion = int(MaterializeType::Occlusion);
  const int thickness = int(MaterializeType::Thickness);

  b.add_input<decl::Menu>("Curvature")
      .static_items(curvature_items)
      .default_value(MaterializeCurvature::Convex)
      .optional_label()
      .usage_by_menu("Type", curvature)
      .description("Edges, creases or both");
  b.add_input<decl::Float>("Curvature Angle")
      .default_value(0.5235988f)
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Type", curvature)
      .description("How sharp an edge or crease must be");
  b.add_input<decl::Menu>("Direction")
      .static_items(direction_items)
      .default_value(MaterializeDirection::Up)
      .optional_label()
      .usage_by_menu("Type", direction)
      .description("Which way the faces face");
  b.add_input<decl::Vector>("Custom Direction")
      .default_value(float3(0.0f, 0.0f, 1.0f))
      .usage_by_menu("Type", direction)
      .description("Direction for Custom");
  b.add_input<decl::Float>("Direction Angle")
      .default_value(0.7853982f)
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Type", direction)
      .description("How far from the direction a face may turn");
  b.add_input<decl::Menu>("Axis")
      .static_items(axis_items)
      .default_value(MenuValue(2))
      .usage_by_menu("Type", height)
      .description("Axis of the height");
  b.add_input<decl::Float>("Height From")
      .default_value(0.0f)
      .usage_by_menu("Type", height)
      .description("Bottom of the band");
  b.add_input<decl::Float>("Height To")
      .default_value(0.5f)
      .usage_by_menu("Type", height)
      .description("Top of the band");
  b.add_input<decl::Bool>("Relative")
      .default_value(true)
      .usage_by_menu("Type", height)
      .description("Heights go from 0 at the bottom of the mesh to 1 at the top");
  b.add_input<decl::Float>("Slope Min")
      .default_value(0.0f)
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Type", slope)
      .description("Flattest slope");
  b.add_input<decl::Float>("Slope Max")
      .default_value(0.5235988f)
      .min(0.0f)
      .max(M_PI)
      .subtype(PROP_ANGLE)
      .usage_by_menu("Type", slope)
      .description("Steepest slope");
  b.add_input<decl::Float>("Noise Scale")
      .default_value(2.0f)
      .min(0.0f)
      .usage_by_menu("Type", noise)
      .description("Size of the patches. Higher is smaller");
  b.add_input<decl::Float>("Noise Detail")
      .default_value(2.0f)
      .min(0.0f)
      .max(15.0f)
      .usage_by_menu("Type", noise)
      .description("Layers of finer noise");
  b.add_input<decl::Float>("Noise Threshold")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu("Type", noise)
      .description("Higher gives fewer patches");
  b.add_input<decl::Menu>("Random")
      .static_items(random_items)
      .default_value(MaterializeRandom::Face)
      .optional_label()
      .usage_by_menu("Type", random)
      .description("What is picked at random");
  b.add_input<decl::Float>("Chance")
      .default_value(0.3f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu("Type", random)
      .description("How likely each is picked");
  b.add_input<decl::Float>("Value")
      .default_value(0.0f)
      .field_on_all()
      .usage_by_menu("Type", value)
      .description("Value per face, from an attribute or a texture");
  b.add_input<decl::Float>("Threshold")
      .default_value(0.5f)
      .usage_by_menu("Type", value)
      .description("Faces with a higher value are picked");
  b.add_input<decl::Geometry>("Target")
      .only_realized_data()
      .supported_type(GeometryComponent::Type::Mesh)
      .usage_by_menu("Type", proximity)
      .description("Mesh to measure the distance to");
  b.add_input<decl::Float>("Distance")
      .default_value(0.5f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", proximity)
      .description("Faces closer than this are picked");
  b.add_input<decl::Float>("Occlusion")
      .default_value(0.5f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .usage_by_menu("Type", occlusion)
      .description("How hidden a face must be");
  b.add_input<decl::Float>("Occlusion Distance")
      .default_value(0.5f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", occlusion)
      .description("How far to look for things that hide a face");
  b.add_input<decl::Float>("Thickness")
      .default_value(0.1f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .usage_by_menu("Type", thickness)
      .description("Faces where the mesh is thinner than this are picked");
  b.add_input<decl::Int>("Samples")
      .default_value(16)
      .min(1)
      .max(256)
      .usage_by_menu("Type", {occlusion, thickness})
      .description("Rays per face. More is smoother and slower");

  PanelDeclarationBuilder &shape = b.add_panel("Shape").default_closed(true);
  shape.add_input<decl::Int>("Grow").default_value(0).min(-100).max(100).description(
      "Adds rings of faces around the picked faces, or removes them when negative");
  shape.add_input<decl::Int>("Min Island").default_value(0).min(0).description(
      "Patches of fewer faces are dropped");
  shape.add_input<decl::Float>("Breakup")
      .default_value(0.0f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Breaks up the border with noise");
  shape.add_input<decl::Float>("Breakup Scale").default_value(5.0f).min(0.0f).description(
      "Size of the breakup noise. Higher is smaller");
  shape.add_input<decl::Bool>("Invert").default_value(false).description(
      "Pick the other faces");
  shape.add_input<decl::Int>("Seed").default_value(0).description("Changes the noise and random");
}

static int ensure_material_index(Mesh &mesh, Material *material)
{
  const int materials_num = mesh.totcol;
  if (mesh.mat && materials_num > 0) {
    const int index = Span(mesh.mat, materials_num).first_index_try(material);
    if (index != -1) {
      return index;
    }
  }
  BKE_id_material_eval_assign(&mesh.id, materials_num + 1, material);
  return materials_num;
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");
  Material *material = params.extract_input<Material *>("Material");
  const Field<float> value_field = params.extract_input<Field<float>>("Value");
  const GeometrySet target_set = params.extract_input<GeometrySet>("Target");

  geometry::MaterializeRule rule;
  rule.type = params.extract_input<MaterializeType>("Type");
  rule.curvature = params.extract_input<MaterializeCurvature>("Curvature");
  rule.curvature_angle = params.extract_input<float>("Curvature Angle");
  rule.direction = params.extract_input<MaterializeDirection>("Direction");
  rule.custom_direction = params.extract_input<float3>("Custom Direction");
  rule.direction_angle = params.extract_input<float>("Direction Angle");
  rule.axis = params.extract_input<MenuValue>("Axis").value;
  rule.height_from = params.extract_input<float>("Height From");
  rule.height_to = params.extract_input<float>("Height To");
  rule.relative = params.extract_input<bool>("Relative");
  rule.slope_min = params.extract_input<float>("Slope Min");
  rule.slope_max = params.extract_input<float>("Slope Max");
  rule.noise_scale = params.extract_input<float>("Noise Scale");
  rule.noise_detail = params.extract_input<float>("Noise Detail");
  rule.noise_threshold = params.extract_input<float>("Noise Threshold");
  rule.random = params.extract_input<MaterializeRandom>("Random");
  rule.chance = params.extract_input<float>("Chance");
  rule.threshold = params.extract_input<float>("Threshold");
  rule.distance = params.extract_input<float>("Distance");
  rule.occlusion = params.extract_input<float>("Occlusion");
  rule.occlusion_distance = params.extract_input<float>("Occlusion Distance");
  rule.thickness = params.extract_input<float>("Thickness");
  rule.samples = params.extract_input<int>("Samples");
  rule.grow = params.extract_input<int>("Grow");
  rule.min_island = params.extract_input<int>("Min Island");
  rule.breakup = params.extract_input<float>("Breakup");
  rule.breakup_scale = params.extract_input<float>("Breakup Scale");
  rule.invert = params.extract_input<bool>("Invert");
  rule.seed = params.extract_input<int>("Seed");

  const std::optional<std::string> picked_id =
      params.get_output_anonymous_attribute_id_if_needed("Picked");
  const Mesh *target = target_set.get_mesh();

  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    if (!geometry_set.has_mesh()) {
      return;
    }
    Mesh &mesh = *geometry_set.get_mesh_for_write();
    if (mesh.faces_num == 0) {
      return;
    }
    const bke::MeshFieldContext context(mesh, bke::AttrDomain::Face);
    FieldEvaluator evaluator(context, mesh.faces_num);
    evaluator.add(selection_field);
    if (rule.type == MaterializeType::Attribute) {
      evaluator.add(value_field);
    }
    evaluator.evaluate();
    const VArraySpan<bool> selection = evaluator.get_evaluated<bool>(0);
    VArraySpan<float> values;
    Array<float> distances;

    geometry::MaterializeRule mesh_rule = rule;
    if (rule.type == MaterializeType::Attribute) {
      values = evaluator.get_evaluated<float>(1);
      mesh_rule.values = values;
    }
    else if (rule.type == MaterializeType::Proximity) {
      if (target) {
        distances = geometry::materialize_face_distances(mesh, *target, float4x4::identity());
      }
      else {
        distances = Array<float>(mesh.faces_num, FLT_MAX);
      }
      mesh_rule.distances = distances;
    }

    if (mesh.totcol == 0) {
      BKE_id_material_eval_ensure_default_slot(&mesh.id);
    }
    mesh_rule.material = ensure_material_index(mesh, material);

    bke::MutableAttributeAccessor attributes = mesh.attributes_for_write();
    const Array<int> original = [&]() {
      const VArraySpan<int> read = *attributes.lookup_or_default<int>(
          "material_index", bke::AttrDomain::Face, 0);
      return Array<int>(read);
    }();

    const std::string rule_id = ".materialize_picked";
    geometry::MeshMaterializeParams settings;
    settings.rules = Span(&mesh_rule, 1);
    settings.rule_id = rule_id;
    geometry::mesh_materialize(mesh, settings);

    const VArraySpan<int> picked_rule = *attributes.lookup<int>(rule_id, bke::AttrDomain::Face);
    Array<bool> picked(mesh.faces_num, false);
    bke::SpanAttributeWriter<int> material_index =
        attributes.lookup_or_add_for_write_span<int>("material_index", bke::AttrDomain::Face);
    for (const int f : IndexRange(mesh.faces_num)) {
      if (!selection[f]) {
        material_index.span[f] = original[f];
      }
      else if (!picked_rule.is_empty() && picked_rule[f] == 0) {
        picked[f] = true;
      }
    }
    material_index.finish();
    attributes.remove(rule_id);

    if (picked_id) {
      attributes.add<bool>(*picked_id,
                           bke::AttrDomain::Face,
                           bke::AttributeInitVArray(VArray<bool>::from_span(picked)));
    }
  });
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeMaterialize");
  ntype.ui_name = "Materialize";
  ntype.ui_description =
      "Give a material to faces picked by curvature, direction, height, slope, noise and more";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_materialize_cc

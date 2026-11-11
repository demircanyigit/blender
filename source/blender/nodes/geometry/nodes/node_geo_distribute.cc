/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include "BLI_math_vector.hh"
#include "BLI_noise.hh"

#include "BKE_instances.hh"
#include "BKE_mesh.hh"

#include "GEO_mesh_distribute.hh"
#include "GEO_realize_instances.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_distribute_cc {

using geometry::DistributeDirection;
using geometry::DistributeRotation;
using geometry::StoneworkColorMode;
using geometry::StoneworkRemesh;

static const EnumPropertyItem rotation_items[] = {
    {int(DistributeRotation::Align),
     "ALIGN",
     ICON_NONE,
     N_("Align"),
     N_("Lined up with the direction, turned by the jitter")},
    {int(DistributeRotation::Steps),
     "STEPS",
     ICON_NONE,
     N_("90° Steps"),
     N_("Turned in steps of 90 degrees, plus the jitter")},
    {int(DistributeRotation::Random), "RANDOM", ICON_NONE, N_("Random"), N_("Turned at random")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem direction_items[] = {
    {int(DistributeDirection::Auto),
     "AUTO",
     ICON_NONE,
     N_("Auto"),
     N_("Level on walls, along X on floors")},
    {int(DistributeDirection::WorldX), "X", ICON_NONE, N_("X"), N_("Along world X")},
    {int(DistributeDirection::WorldY), "Y", ICON_NONE, N_("Y"), N_("Along world Y")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem remesh_items[] = {
    {int(StoneworkRemesh::Isotropic),
     "ISOTROPIC",
     ICON_NONE,
     N_("Isotropic"),
     N_("Even triangles that follow the surface")},
    {int(StoneworkRemesh::Voxel),
     "VOXEL",
     ICON_NONE,
     N_("Voxel"),
     N_("A new closed surface made from voxels")},
    {int(StoneworkRemesh::Divide), "DIVIDE", ICON_NONE, N_("Divide"), N_("The faces cut by a grid")},
    {int(StoneworkRemesh::None), "NONE", ICON_NONE, N_("None"), N_("The faces as they are")},
    {0, nullptr, 0, nullptr, nullptr},
};

static const EnumPropertyItem color_mode_items[] = {
    {int(StoneworkColorMode::Hue), "HUE", ICON_NONE, N_("Hue"), N_("Random hues")},
    {int(StoneworkColorMode::Gray), "GRAY", ICON_NONE, N_("Gray"), N_("Random shades of gray")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Surface")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Faces to place the objects on");
  b.add_output<decl::Geometry>("Geometry").align_with_previous();
  b.add_output<decl::Int>("ID").field_on_all().description("Which object each instance is");
  b.add_output<decl::Float>("Random")
      .field_on_all()
      .description("A random value from 0 to 1 for each instance");
  b.add_output<decl::Color>("Color")
      .field_on_all()
      .description("A random color for each instance");
  b.add_output<decl::Bool>("Filler")
      .field_on_all()
      .description("True on the instances that fill the gaps");

  b.add_input<decl::Geometry>("Instances").description(
      "The objects to place. Each top level instance is one object, for example from Collection "
      "Info with Separate Children");
  b.add_input<decl::Geometry>("Fillers").description(
      "Smaller objects for the gaps at the end. Leave empty to use smaller copies of the others");
  b.add_input<decl::Bool>("Selection").default_value(true).hide_value().field_on_all();

  PanelDeclarationBuilder &pack = b.add_panel("Packing").default_closed(false);
  pack.add_input<decl::Float>("Gap")
      .default_value(0.01f)
      .min(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Space between the objects");
  pack.add_input<decl::Int>("Tries")
      .default_value(24)
      .min(1)
      .max(1000)
      .description("How many spots are tried for each object. More packs tighter, slower");
  pack.add_input<decl::Bool>("Stay Inside")
      .default_value(true)
      .description("Objects must not hang over the edges of the surface");
  pack.add_input<decl::Bool>("Fill Gaps")
      .default_value(true)
      .description("Fill the gaps at the end with the fillers, or smaller copies of the objects");
  pack.add_input<decl::Float>("Filler Scale")
      .default_value(0.5f)
      .min(0.05f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("Size of the smaller copies, when there are no fillers");
  pack.add_input<decl::Int>("Max Count")
      .default_value(5000)
      .min(0)
      .max(1000000)
      .description("Stop after this many objects");
  pack.add_input<decl::Int>("Seed").description("Random seed");

  PanelDeclarationBuilder &place = b.add_panel("Placement").default_closed(false);
  place.add_input<decl::Bool>("Align to Normal")
      .default_value(true)
      .description("Objects stand on the faces. Otherwise they stay upright");
  place.add_input<decl::Menu>("Direction")
      .static_items(direction_items)
      .default_value(DistributeDirection::Auto)
      .optional_label()
      .description("Which way the objects' X axis points");
  place.add_input<decl::Menu>("Rotation")
      .static_items(rotation_items)
      .default_value(DistributeRotation::Align)
      .optional_label()
      .description("How the objects are turned");
  place.add_input<decl::Float>("Jitter")
      .default_value(0.0f)
      .min(0.0f)
      .max(float(M_PI))
      .subtype(PROP_ANGLE)
      .description("Largest random turn");
  place.add_input<decl::Float>("Scale Min").default_value(1.0f).min(0.0f).description(
      "Smallest random size");
  place.add_input<decl::Float>("Scale Max").default_value(1.0f).min(0.0f).description(
      "Largest random size");
  place.add_input<decl::Float>("Offset")
      .default_value(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance from the bottom of each object to the surface, negative sinks it");

  PanelDeclarationBuilder &mortar = b.add_panel("Mortar").default_closed(true);
  mortar.add_input<decl::Bool>("Mortar")
      .default_value(false)
      .panel_toggle()
      .description("Replace the surface with mortar: a dense copy with bumps");
  mortar.add_input<decl::Float>("Mortar Offset")
      .default_value(0.0f)
      .subtype(PROP_DISTANCE)
      .description("Distance of the mortar along the normals, negative goes in");
  mortar.add_input<decl::Menu>("Remesh")
      .static_items(remesh_items)
      .default_value(StoneworkRemesh::Isotropic)
      .optional_label()
      .description("How the mortar is made dense");
  mortar.add_input<decl::Float>("Resolution")
      .default_value(0.05f)
      .min(0.001f)
      .subtype(PROP_DISTANCE)
      .description("Edge length, voxel size or grid size of the mortar");
  mortar.add_input<decl::Float>("Noise Strength")
      .default_value(0.01f)
      .subtype(PROP_DISTANCE)
      .description("Height of the bumps");
  mortar.add_input<decl::Float>("Noise Scale")
      .default_value(5.0f)
      .min(0.0f)
      .description("Size of the bumps, higher is smaller");
  mortar.add_input<decl::Float>("Noise Detail")
      .default_value(3.0f)
      .min(0.0f)
      .max(15.0f)
      .description("Small bumps on the big ones");
  b.add_input<decl::Bool>("Keep Surface")
      .default_value(true)
      .description("Keep the surface when there is no mortar");

  PanelDeclarationBuilder &color = b.add_panel("Color").default_closed(true);
  color.add_input<decl::Menu>("Color Mode")
      .static_items(color_mode_items)
      .default_value(StoneworkColorMode::Hue)
      .optional_label()
      .description("What kind of random colors the Color output has");
  auto range = [&](const char *name, const float value, const char *description) {
    color.add_input<decl::Float>(name)
        .default_value(value)
        .min(0.0f)
        .max(1.0f)
        .subtype(PROP_FACTOR)
        .description(description);
  };
  range("Hue Min", 0.0f, "Lowest hue");
  range("Hue Max", 1.0f, "Highest hue");
  range("Saturation Min", 0.45f, "Lowest saturation");
  range("Saturation Max", 0.85f, "Highest saturation");
  range("Value Min", 0.65f, "Darkest value");
  range("Value Max", 0.95f, "Brightest value");
}

/** The objects of an input: each top level instance, or the whole geometry. */
static void gather_items(const GeometrySet &geometry,
                         const bool filler,
                         Vector<geometry::DistributeItem> &r_items,
                         Vector<bke::InstanceReference> &r_refs,
                         Vector<float3x3> &r_shapes)
{
  /* Each item is measured turned and scaled as its first instance is, but not moved. */
  auto add = [&](const bke::InstanceReference &reference, const float3x3 &shape) {
    GeometrySet set;
    reference.to_geometry_set(set);
    if (set.has_instances()) {
      set = geometry::realize_instances(set, geometry::RealizeInstancesOptions()).geometry;
    }
    const std::optional<Bounds<float3>> bounds = set.compute_boundbox_without_instances();
    if (!bounds) {
      return;
    }
    float3 lo(1e30f), hi(-1e30f);
    for (int i = 0; i < 8; i++) {
      const float3 corner((i & 1) ? bounds->max.x : bounds->min.x,
                          (i & 2) ? bounds->max.y : bounds->min.y,
                          (i & 4) ? bounds->max.z : bounds->min.z);
      const float3 p = shape * corner;
      lo = math::min(lo, p);
      hi = math::max(hi, p);
    }
    geometry::DistributeItem item;
    item.min = lo;
    item.max = hi;
    item.filler = filler;
    r_items.append(item);
    r_refs.append(reference);
    r_shapes.append(shape);
  };
  if (const bke::Instances *instances = geometry.get_instances()) {
    const Span<bke::InstanceReference> refs = instances->references();
    const Span<int> handles = instances->reference_handles();
    const Span<float4x4> transforms = instances->transforms();
    Vector<int> seen;
    for (const int i : handles.index_range()) {
      const int handle = handles[i];
      if (!seen.contains(handle)) {
        seen.append(handle);
        add(refs[handle], float3x3(transforms[i]));
      }
    }
  }
  GeometrySet rest = geometry;
  rest.remove<bke::InstancesComponent>();
  if (!rest.is_empty()) {
    add(bke::InstanceReference(rest), float3x3::identity());
  }
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet surface_set = params.extract_input<GeometrySet>("Surface");
  const GeometrySet objects = params.extract_input<GeometrySet>("Instances");
  const GeometrySet fillers = params.extract_input<GeometrySet>("Fillers");
  const Field<bool> selection_field = params.extract_input<Field<bool>>("Selection");

  Vector<geometry::DistributeItem> items;
  Vector<bke::InstanceReference> refs;
  Vector<float3x3> shapes;
  gather_items(objects, false, items, refs, shapes);
  gather_items(fillers, true, items, refs, shapes);

  geometry::DistributeParams settings;
  settings.gap = params.extract_input<float>("Gap");
  settings.tries = params.extract_input<int>("Tries");
  settings.stay_inside = params.extract_input<bool>("Stay Inside");
  settings.fillers = params.extract_input<bool>("Fill Gaps");
  settings.filler_scale = params.extract_input<float>("Filler Scale");
  settings.max_count = params.extract_input<int>("Max Count");
  settings.seed = params.extract_input<int>("Seed");
  settings.align_to_normal = params.extract_input<bool>("Align to Normal");
  settings.direction = params.extract_input<DistributeDirection>("Direction");
  settings.rotation = params.extract_input<DistributeRotation>("Rotation");
  settings.jitter = params.extract_input<float>("Jitter");
  settings.scale_min = params.extract_input<float>("Scale Min");
  settings.scale_max = params.extract_input<float>("Scale Max");
  settings.offset = params.extract_input<float>("Offset");
  settings.items = items;

  geometry::DistributeMortarParams mortar;
  const bool use_mortar = params.extract_input<bool>("Mortar");
  mortar.offset = params.extract_input<float>("Mortar Offset");
  mortar.remesh = params.extract_input<StoneworkRemesh>("Remesh");
  mortar.resolution = params.extract_input<float>("Resolution");
  mortar.noise_strength = params.extract_input<float>("Noise Strength");
  mortar.noise_scale = params.extract_input<float>("Noise Scale");
  mortar.noise_detail = params.extract_input<float>("Noise Detail");
  mortar.seed = settings.seed;
  const bool keep_surface = params.extract_input<bool>("Keep Surface");

  const StoneworkColorMode color_mode = params.extract_input<StoneworkColorMode>("Color Mode");
  const float2 hue(params.extract_input<float>("Hue Min"), params.extract_input<float>("Hue Max"));
  const float2 sat(params.extract_input<float>("Saturation Min"),
                   params.extract_input<float>("Saturation Max"));
  const float2 val(params.extract_input<float>("Value Min"),
                   params.extract_input<float>("Value Max"));

  const std::optional<std::string> id_id = params.get_output_anonymous_attribute_id_if_needed(
      "ID");
  const std::optional<std::string> random_id = params.get_output_anonymous_attribute_id_if_needed(
      "Random");
  const std::optional<std::string> color_id = params.get_output_anonymous_attribute_id_if_needed(
      "Color");
  const std::optional<std::string> filler_id = params.get_output_anonymous_attribute_id_if_needed(
      "Filler");

  const Mesh *surface = surface_set.get_mesh();
  if (surface == nullptr) {
    params.set_output("Geometry", std::move(surface_set));
    return;
  }

  /* Which faces. */
  const bke::MeshFieldContext context(*surface, bke::AttrDomain::Face);
  FieldEvaluator evaluator(context, surface->faces_num);
  evaluator.add(selection_field);
  evaluator.evaluate();
  const VArraySpan<bool> selection = evaluator.get_evaluated<bool>(0);
  const bool all = selection.is_empty() || !selection.contains(false);
  if (!all) {
    settings.selection = selection;
    mortar.selection = selection;
  }

  Vector<geometry::DistributePlacement> placed;
  if (!items.is_empty()) {
    placed = geometry::distribute_by_size(*surface, settings);
  }

  std::unique_ptr<bke::Instances> instances = std::make_unique<bke::Instances>(placed.size());
  Array<int> handle_of(refs.size(), -1);
  MutableSpan<int> handles = instances->reference_handles_for_write();
  MutableSpan<float4x4> transforms = instances->transforms_for_write();
  for (const int i : placed.index_range()) {
    const int item = placed[i].item;
    if (handle_of[item] < 0) {
      handle_of[item] = instances->add_reference(refs[item]);
    }
    handles[i] = handle_of[item];
    transforms[i] = placed[i].transform * float4x4(shapes[placed[i].item]);
  }
  bke::MutableAttributeAccessor attributes = instances->attributes_for_write();
  if (id_id) {
    bke::SpanAttributeWriter<int> w = attributes.lookup_or_add_for_write_only_span<int>(
        *id_id, bke::AttrDomain::Instance);
    for (const int i : placed.index_range()) {
      w.span[i] = i;
    }
    w.finish();
  }
  if (random_id) {
    bke::SpanAttributeWriter<float> w = attributes.lookup_or_add_for_write_only_span<float>(
        *random_id, bke::AttrDomain::Instance);
    for (const int i : placed.index_range()) {
      w.span[i] = float(noise::hash(uint32_t(i), uint32_t(settings.seed), 77u)) / 4294967295.0f;
    }
    w.finish();
  }
  if (color_id) {
    bke::SpanAttributeWriter<ColorGeometry4f> w =
        attributes.lookup_or_add_for_write_only_span<ColorGeometry4f>(*color_id,
                                                                      bke::AttrDomain::Instance);
    for (const int i : placed.index_range()) {
      const float4 c = geometry::distribute_random_color(
          i, settings.seed, color_mode, hue, sat, val);
      w.span[i] = ColorGeometry4f(c.x, c.y, c.z, c.w);
    }
    w.finish();
  }
  if (filler_id) {
    bke::SpanAttributeWriter<bool> w = attributes.lookup_or_add_for_write_only_span<bool>(
        *filler_id, bke::AttrDomain::Instance);
    for (const int i : placed.index_range()) {
      w.span[i] = placed[i].filler;
    }
    w.finish();
  }

  GeometrySet result;
  if (use_mortar) {
    std::string message;
    Mesh *mortar_mesh = geometry::distribute_mortar(*surface, mortar, message);
    if (!message.empty()) {
      params.error_message_add(NodeWarningType::Warning, message);
    }
    if (mortar_mesh) {
      result.replace_mesh(mortar_mesh);
    }
  }
  else if (keep_surface) {
    result = surface_set;
    result.remove<bke::InstancesComponent>();
  }
  result.replace_instances(instances.release());
  params.set_output("Geometry", std::move(result));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeDistribute");
  ntype.ui_name = "Distribute";
  ntype.ui_description =
      "Pack objects on a surface by their measured sides, so none of them touch";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_distribute_cc

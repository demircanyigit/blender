/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <mutex>

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "DEG_depsgraph_query.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_uv_scale.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_uv_scale_cc {

enum class Mode { Scale = 0, Density = 1 };
enum class Pivot { Center = 0, Origin = 1, Custom = 2 };

static void node_declare(NodeDeclarationBuilder &b)
{
  static const EnumPropertyItem modes[] = {
      {0, "SCALE", ICON_NONE, N_("Scale"), N_("Multiply the existing UV coordinates")},
      {1, "DENSITY", ICON_NONE, N_("Texel Density"), N_("Set pixels per meter of surface")},
      {0, nullptr, 0, nullptr, nullptr},
  };
  static const EnumPropertyItem pivots[] = {
      {0, "CENTER", ICON_NONE, N_("Bounds Center"), N_("Center of the UV bounds")},
      {1, "ORIGIN", ICON_NONE, N_("UV Origin"), N_("The UV origin, 0, 0")},
      {2, "CUSTOM", ICON_NONE, N_("Custom"), N_("Use the Pivot Position input")},
      {0, nullptr, 0, nullptr, nullptr},
  };
  b.use_custom_socket_order();
  b.add_input<decl::Geometry>("Mesh").supported_type(GeometryComponent::Type::Mesh);
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();
  b.add_input<decl::String>("UV Map").description("Existing UV map; empty uses the active map");
  b.add_input<decl::Menu>("Mode").static_items(modes).default_value(Mode::Scale);
  b.add_input<decl::Bool>("Individual Islands")
      .default_value(true)
      .description("Scale or normalize each UV island separately; otherwise use all UVs together");
  b.add_input<decl::Vector>("Scale")
      .default_value({1, 1, 1})
      .dimensions(2)
      .usage_by_menu("Mode", 0)
      .description("Multiply U and V around the chosen pivot");
  b.add_input<decl::Float>("Density")
      .default_value(1024)
      .min(0.001f)
      .max(1.0e9f)
      .usage_by_menu("Mode", 1)
      .description("Target pixels per meter, using scene units and object scale");
  b.add_input<decl::Int>("Texture Width")
      .default_value(1024)
      .min(1)
      .max(65536)
      .usage_by_menu("Mode", 1)
      .description("Texture width in pixels");
  b.add_input<decl::Int>("Texture Height")
      .default_value(1024)
      .min(1)
      .max(65536)
      .usage_by_menu("Mode", 1)
      .description("Texture height in pixels");
  b.add_input<decl::Menu>("Pivot").static_items(pivots).default_value(Pivot::Center);
  b.add_input<decl::Vector>("Pivot Position")
      .default_value({0.5f, 0.5f, 0})
      .dimensions(2)
      .usage_by_menu("Pivot", 2)
      .description("Custom pivot in UV coordinates");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  const std::string uv_map = params.extract_input<std::string>("UV Map");
  geometry::UVScaleParams settings;
  settings.texel_density = params.extract_input<Mode>("Mode") == Mode::Density;
  settings.individual_islands = params.extract_input<bool>("Individual Islands");
  settings.pivot = int(params.extract_input<Pivot>("Pivot"));
  if (settings.pivot == int(Pivot::Custom)) {
    const float3 center = params.extract_input<float3>("Pivot Position");
    settings.center = float2(center.x, center.y);
  }
  if (settings.texel_density) {
    settings.density = params.extract_input<float>("Density");
    settings.resolution = int2(params.extract_input<int>("Texture Width"),
                               params.extract_input<int>("Texture Height"));
    if (const Object *object = params.self_object()) {
      settings.object_to_world = object->object_to_world();
    }
    if (const Depsgraph *depsgraph = params.depsgraph()) {
      settings.meters_per_unit = DEG_get_evaluated_scene(depsgraph)->unit.scale_length;
    }
  }
  else {
    const float3 scale = params.extract_input<float3>("Scale");
    settings.scale = float2(scale.x, scale.y);
  }
  std::mutex mutex;
  const char *error = nullptr;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &part) {
    if (Mesh *mesh = part.get_mesh_for_write()) {
      if (const char *message = geometry::uv_scale(*mesh, uv_map, settings)) {
        std::lock_guard lock(mutex);
        error = message;
      }
    }
  });
  if (error) {
    params.error_message_add(NodeWarningType::Warning, TIP_(error));
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static bke::bNodeType ntype;
  geo_node_type_base(&ntype, "GeometryNodeUVScale");
  ntype.ui_name = "UV Scale";
  ntype.ui_description = "Scale UVs globally or per island, or match a world-space texel density";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_uv_scale_cc

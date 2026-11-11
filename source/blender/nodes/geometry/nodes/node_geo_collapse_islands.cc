/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#include <mutex>

#include "DNA_mesh_types.h"
#include "DNA_object_types.h"

#include "BLI_array.hh"
#include "BLI_math_matrix.hh"

#include "GEO_foreach_geometry.hh"
#include "GEO_mesh_collapse.hh"
#include "GEO_realize_instances.hh"

#include "UI_resources.hh"

#include "node_geometry_util.hh"

namespace blender::nodes::node_geo_collapse_islands_cc {

enum class Space {
  Object = 0,
  World = 1,
};

static const EnumPropertyItem space_items[] = {
    {int(Space::Object),
     "OBJECT",
     ICON_NONE,
     N_("Object"),
     N_("Fall along the -Z axis of the object, so down turns with it")},
    {int(Space::World),
     "WORLD",
     ICON_NONE,
     N_("World"),
     N_("Fall along the -Z axis of the world, so down stays down when the object turns")},
    {0, nullptr, 0, nullptr, nullptr},
};

static void node_declare(NodeDeclarationBuilder &b)
{
  b.use_custom_socket_order();
  b.allow_any_socket_order();

  b.add_input<decl::Geometry>("Mesh")
      .supported_type(GeometryComponent::Type::Mesh)
      .is_default_link_socket()
      .description("Mesh whose loose pieces fall");
  b.add_output<decl::Geometry>("Mesh").propagate_all().align_with_previous();

  b.add_input<decl::Geometry>("Ground")
      .supported_type({GeometryComponent::Type::Mesh, GeometryComponent::Type::Instance})
      .description("Meshes the pieces land on. Instances, like a whole collection, are used too. "
                   "Without any, they land on a flat floor under the lowest point");
  b.add_input<decl::Float>("Cage")
      .subtype(PROP_DISTANCE)
      .description("Push the ground out along its normals by this much, so the pieces land "
                   "further away. Negative pulls it in");
  b.add_input<decl::Bool>("Keep")
      .default_value(false)
      .hide_value()
      .field_on_all()
      .description("Pieces with any of these points stay where they are");
  b.add_input<decl::Menu>("Space")
      .static_items(space_items)
      .default_value(Space::World)
      .description("Which way is down");
  b.add_input<decl::Float>("Time")
      .default_value(3.0f)
      .min(0.0f)
      .max(60.0f)
      .subtype(PROP_TIME_ABSOLUTE)
      .description("Seconds the pieces fall for. Longer lets them settle more");
  b.add_input<decl::Float>("Gravity")
      .default_value(9.81f)
      .min(0.0f)
      .subtype(PROP_ACCELERATION)
      .description("How hard the pieces are pulled down");
  b.add_input<decl::Float>("Push")
      .default_value(1.5f)
      .min(0.0f)
      .subtype(PROP_VELOCITY)
      .description("Random start speed and spin of each piece, so stacks tip over");
  b.add_input<decl::Int>("Seed").default_value(0).description("Changes the random push");
  b.add_input<decl::Float>("Friction")
      .default_value(0.5f)
      .min(0.0f)
      .max(10.0f)
      .subtype(PROP_FACTOR)
      .description("How much the pieces resist sliding");
  b.add_input<decl::Float>("Bounce")
      .default_value(0.1f)
      .min(0.0f)
      .max(1.0f)
      .subtype(PROP_FACTOR)
      .description("How much the pieces bounce when they hit something");
  b.add_input<decl::Int>("Quality")
      .default_value(4)
      .min(1)
      .max(100)
      .description(
          "Solver steps per 1/60 second. Higher keeps small or fast pieces from passing through");
}

static void node_geo_exec(GeoNodeExecParams params)
{
  GeometrySet geometry_set = params.extract_input<GeometrySet>("Mesh");
  GeometrySet ground_set = params.extract_input<GeometrySet>("Ground");
  if (ground_set.has_instances()) {
    ground_set = geometry::realize_instances(ground_set, geometry::RealizeInstancesOptions())
                     .geometry;
  }
  const Field<bool> keep_field = params.extract_input<Field<bool>>("Keep");
  const Space space = params.extract_input<Space>("Space");

  geometry::MeshCollapseParams settings;
  settings.time = params.extract_input<float>("Time");
  settings.gravity = params.extract_input<float>("Gravity");
  settings.push = params.extract_input<float>("Push");
  settings.seed = params.extract_input<int>("Seed");
  settings.friction = params.extract_input<float>("Friction");
  settings.bounce = params.extract_input<float>("Bounce");
  settings.substeps = params.extract_input<int>("Quality");

  const Object *self_object = params.self_object();
  if (space == Space::World && self_object != nullptr) {
    settings.mesh_to_space = self_object->object_to_world();
  }
  /* The ground comes in the same space as the mesh. */
  settings.ground = ground_set.get_mesh();
  settings.ground_to_space = settings.mesh_to_space;
  settings.ground_cage = params.extract_input<float>("Cage");

  std::mutex error_mutex;
  const char *error = nullptr;
  geometry::foreach_real_geometry(geometry_set, [&](GeometrySet &geometry_set) {
    const Mesh *src_mesh = geometry_set.get_mesh();
    if (src_mesh == nullptr || src_mesh->verts_num == 0) {
      return;
    }
    geometry::MeshCollapseParams mesh_settings = settings;
    Array<bool> keep(src_mesh->verts_num);
    const bke::MeshFieldContext context{*src_mesh, AttrDomain::Point};
    FieldEvaluator evaluator{context, src_mesh->verts_num};
    evaluator.add_with_destination(keep_field, keep.as_mutable_span());
    evaluator.evaluate();
    mesh_settings.keep = keep;

    const char *mesh_error = nullptr;
    Mesh *result = geometry::mesh_collapse(*src_mesh, mesh_settings, &mesh_error);
    if (mesh_error != nullptr) {
      std::lock_guard lock{error_mutex};
      error = mesh_error;
    }
    if (result != nullptr) {
      geometry_set.replace_mesh(result);
    }
  });

  if (error != nullptr) {
    params.error_message_add(NodeWarningType::Error, TIP_(error));
  }
  params.set_output("Mesh", std::move(geometry_set));
}

static void node_register()
{
  static blender::bke::bNodeType ntype;

  geo_node_type_base(&ntype, "GeometryNodeCollapseIslands");
  ntype.ui_name = "Collapse Islands";
  ntype.ui_description =
      "Let every loose piece of a mesh fall and keep the pile where it comes to rest";
  ntype.nclass = NODE_CLASS_GEOMETRY;
  ntype.declare = node_declare;
  ntype.geometry_node_execute = node_geo_exec;
  blender::bke::node_register_type(ntype);
}
NOD_REGISTER_NODE(node_register)

}  // namespace blender::nodes::node_geo_collapse_islands_cc

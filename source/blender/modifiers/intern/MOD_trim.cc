/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup modifiers
 *
 * Trim: copies of a pattern object laid along the edges of the mesh, like a scalloped band, a
 * molding or a row of studs. The edges are joined into paths; each copy bends along its path and
 * the corners are mitered so neighboring copies meet.
 *
 * Loose edges are followed when the mesh has any, otherwise the open borders of its faces.
 */

#include <algorithm>
#include <cmath>
#include <cstring>

#include "BLI_array.hh"
#include "BLI_index_mask.hh"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_noise.hh"
#include "BLI_vector.hh"

#include "BLT_translation.hh"

#include "DNA_collection_types.h"
#include "DNA_layer_types.h"
#include "DNA_mesh_types.h"
#include "DNA_meshdata_types.h"
#include "DNA_modifier_types.h"
#include "DNA_object_types.h"
#include "DNA_screen_types.h"

#include "BKE_collection.hh"
#include "BKE_geometry_set.hh"
#include "BKE_geometry_set_instances.hh"
#include "BKE_instances.hh"
#include "BKE_lib_query.hh"
#include "BKE_mesh.hh"
#include "BKE_modifier.hh"

#include "DEG_depsgraph_build.hh"

#include "UI_interface_layout.hh"
#include "UI_resources.hh"

#include "RNA_access.hh"
#include "RNA_prototypes.hh"

#include "GEO_mesh_merge_by_distance.hh"
#include "GEO_realize_instances.hh"

#include "MOD_modifiertypes.hh"
#include "MOD_ui_common.hh"
#include "MOD_util.hh"

namespace blender {

static constexpr int MAX_COPIES = 100000;

static void init_data(ModifierData *md)
{
  TrimModifierData *data = reinterpret_cast<TrimModifierData *>(md);
  INIT_DEFAULT_STRUCT_AFTER(data, modifier);
}

static void foreach_ID_link(ModifierData *md, Object *ob, IDWalkFunc walk, void *user_data)
{
  TrimModifierData *tmd = reinterpret_cast<TrimModifierData *>(md);
  walk(user_data, ob, reinterpret_cast<ID **>(&tmd->object), IDWALK_CB_NOP);
  walk(user_data, ob, reinterpret_cast<ID **>(&tmd->collection), IDWALK_CB_USER);
  walk(user_data, ob, reinterpret_cast<ID **>(&tmd->start_object), IDWALK_CB_NOP);
  walk(user_data, ob, reinterpret_cast<ID **>(&tmd->end_object), IDWALK_CB_NOP);
}

static void update_depsgraph(ModifierData *md, const ModifierUpdateDepsgraphContext *ctx)
{
  TrimModifierData *tmd = reinterpret_cast<TrimModifierData *>(md);
  for (Object *ob : {tmd->object, tmd->start_object, tmd->end_object}) {
    if (ob != nullptr) {
      DEG_add_object_relation(ctx->node, ob, DEG_OB_COMP_GEOMETRY, "Trim Modifier");
      DEG_add_object_relation(ctx->node, ob, DEG_OB_COMP_TRANSFORM, "Trim Modifier");
    }
  }
  if (tmd->collection != nullptr) {
    DEG_add_collection_geometry_relation(ctx->node, tmd->collection, "Trim Modifier");
  }
}

static bool is_disabled(const Scene * /*scene*/, ModifierData *md, bool /*use_render_params*/)
{
  const TrimModifierData *tmd = reinterpret_cast<const TrimModifierData *>(md);
  return tmd->object == nullptr && tmd->collection == nullptr;
}

/** A pattern mesh with each point given as (along, up, side), ready to lay on a path. */
struct Pattern {
  bke::GeometrySet geometry;
  const Mesh *mesh = nullptr;
  Array<float3> coords;
  float min_along = 0.0f;
  float length = 1.0f;
};

static bool make_pattern(Object *ob,
                         const Object *self,
                         const TrimModifierData &tmd,
                         const int along_axis,
                         const int up_axis,
                         Pattern &r_pattern)
{
  if (ob == nullptr || ob == self) {
    return false;
  }
  bke::GeometrySet set = bke::object_get_evaluated_geometry_set(*ob);
  if (set.has_instances()) {
    set = geometry::realize_instances(set, geometry::RealizeInstancesOptions()).geometry;
  }
  const Mesh *mesh = set.get_mesh();
  if (mesh == nullptr || mesh->verts_num == 0) {
    return false;
  }
  /* Turned and scaled as the object is, but not moved. */
  const float3x3 shape(ob->object_to_world());
  const int side_axis = 3 - along_axis - up_axis;
  float3 along_dir(0.0f), up_dir(0.0f);
  along_dir[along_axis] = 1.0f;
  up_dir[up_axis] = 1.0f;
  const float side_sign = math::cross(along_dir, up_dir)[side_axis];
  const float roll_cos = std::cos(tmd.roll);
  const float roll_sin = std::sin(tmd.roll);

  const Span<float3> positions = mesh->vert_positions();
  r_pattern.coords.reinitialize(positions.size());
  float lo = 1e30f, hi = -1e30f;
  for (const int v : positions.index_range()) {
    const float3 p = shape * positions[v];
    const float up = p[up_axis];
    const float side = side_sign * p[side_axis];
    const float3 c(p[along_axis] * tmd.scale,
                   (up * roll_cos - side * roll_sin) * tmd.scale * tmd.height,
                   (up * roll_sin + side * roll_cos) * tmd.scale);
    r_pattern.coords[v] = c;
    lo = std::min(lo, c.x);
    hi = std::max(hi, c.x);
  }
  r_pattern.min_along = lo;
  r_pattern.length = std::max(hi - lo, 1e-5f);
  r_pattern.geometry = std::move(set);
  r_pattern.mesh = r_pattern.geometry.get_mesh();
  return true;
}

/** A chain of edges, with a frame at each point. */
struct Path {
  Vector<float3> points;
  Vector<float3> normals;
  bool closed = false;

  /** Distance along the path at each point, with the full length last. */
  Array<float> dist;
  /** Along, up and side directions at each point. */
  Array<float3> along, up, side;
  /** Corners widen sideways in this direction by #miter, so neighboring copies meet. */
  Array<float3> miter_dir;
  Array<float> miter;

  int segments() const
  {
    return closed ? points.size() : points.size() - 1;
  }
  float total() const
  {
    return dist.last();
  }
};

/** Joins the edges into paths. Points where more or fewer than two edges meet end a path. */
static Vector<Path> find_paths(const Mesh &mesh, const bool use_normals)
{
  const Span<int2> edges = mesh.edges();
  const Span<float3> positions = mesh.vert_positions();
  Array<int> face_count(edges.size(), 0);
  for (const int e : mesh.corner_edges()) {
    face_count[e]++;
  }
  bool has_loose = false;
  for (const int e : edges.index_range()) {
    has_loose |= face_count[e] == 0;
  }
  Array<Vector<int, 2>> vert_edges(mesh.verts_num);
  for (const int e : edges.index_range()) {
    const bool use = has_loose ? face_count[e] == 0 : face_count[e] == 1;
    if (use && edges[e][0] != edges[e][1]) {
      vert_edges[edges[e][0]].append(e);
      vert_edges[edges[e][1]].append(e);
    }
  }

  Span<float3> vert_normals;
  if (use_normals && mesh.faces_num > 0) {
    vert_normals = mesh.vert_normals();
  }

  Array<bool> used(edges.size(), false);
  Vector<Path> paths;
  auto walk = [&](int v, int e, const bool cycle) {
    Vector<int> verts = {v};
    const int start = v;
    bool closed = false;
    while (true) {
      used[e] = true;
      const int next = edges[e][0] == v ? edges[e][1] : edges[e][0];
      if (cycle && next == start) {
        closed = true;
        break;
      }
      verts.append(next);
      v = next;
      if (next == start || vert_edges[v].size() != 2) {
        break;
      }
      const int other = vert_edges[v][0] == e ? vert_edges[v][1] : vert_edges[v][0];
      if (used[other]) {
        break;
      }
      e = other;
    }
    Path path;
    path.closed = closed;
    for (const int vert : verts) {
      const float3 &p = positions[vert];
      if (!path.points.is_empty() && math::distance_squared(path.points.last(), p) < 1e-14f) {
        continue;
      }
      path.points.append(p);
      path.normals.append(vert_normals.is_empty() ? float3(0.0f, 0.0f, 1.0f) :
                                                    vert_normals[vert]);
    }
    if (path.closed && path.points.size() > 1 &&
        math::distance_squared(path.points.first(), path.points.last()) < 1e-14f)
    {
      path.points.remove_last();
      path.normals.remove_last();
    }
    if (path.closed && path.points.size() < 3) {
      path.closed = false;
    }
    if (path.points.size() >= 2) {
      paths.append(std::move(path));
    }
  };

  for (const int v : vert_edges.index_range()) {
    if (vert_edges[v].size() == 2) {
      continue;
    }
    for (const int e : vert_edges[v]) {
      if (!used[e]) {
        walk(v, e, false);
      }
    }
  }
  /* What is left are closed loops. */
  for (const int v : vert_edges.index_range()) {
    for (const int e : vert_edges[v]) {
      if (!used[e]) {
        walk(v, e, true);
      }
    }
  }
  return paths;
}

static float3 any_perpendicular(const float3 &dir, const float3 &preferred)
{
  for (const float3 &candidate :
       {preferred, float3(0.0f, 0.0f, 1.0f), float3(0.0f, 1.0f, 0.0f), float3(1.0f, 0.0f, 0.0f)})
  {
    const float3 u = candidate - dir * math::dot(candidate, dir);
    if (math::length_squared(u) > 1e-8f) {
      return math::normalize(u);
    }
  }
  return float3(0.0f, 0.0f, 1.0f);
}

static void compute_frames(Path &path)
{
  const int n = path.points.size();
  const int m = path.segments();
  Array<float3> dir(m);
  path.dist.reinitialize(m + 1);
  path.dist[0] = 0.0f;
  for (const int i : IndexRange(m)) {
    const float3 d = path.points[(i + 1) % n] - path.points[i];
    const float len = math::length(d);
    dir[i] = len > 0.0f ? d / len : (i > 0 ? dir[i - 1] : float3(1.0f, 0.0f, 0.0f));
    path.dist[i + 1] = path.dist[i] + len;
  }
  path.along.reinitialize(n);
  path.up.reinitialize(n);
  path.side.reinitialize(n);
  path.miter_dir.reinitialize(n);
  path.miter.reinitialize(n);
  for (const int i : IndexRange(n)) {
    const bool has_in = path.closed || i > 0;
    const bool has_out = path.closed || i < n - 1;
    const float3 in = has_in ? dir[(i - 1 + m) % m] : float3(0.0f);
    const float3 out = has_out ? dir[i] : float3(0.0f);
    float3 t = has_in && has_out ? in + out : (has_in ? in : out);
    t = math::length_squared(t) > 1e-12f ? math::normalize(t) : (has_out ? out : in);
    const float3 u = any_perpendicular(t, path.normals[i]);
    path.along[i] = t;
    path.up[i] = u;
    path.side[i] = math::cross(t, u);
    path.miter_dir[i] = float3(0.0f);
    path.miter[i] = 1.0f;
    if (has_in && has_out) {
      float3 bend = out - in;
      bend -= t * math::dot(bend, t);
      const float c = math::dot(t, out);
      if (math::length_squared(bend) > 1e-10f && c > 1e-3f) {
        path.miter_dir[i] = math::normalize(bend);
        path.miter[i] = 1.0f / std::max(c, 0.25f);
      }
    }
  }
}

static float3 offset_at(const Path &path, const int i, const float up, const float side)
{
  const float3 o = path.up[i] * up + path.side[i] * side;
  return o + path.miter_dir[i] * (math::dot(o, path.miter_dir[i]) * (path.miter[i] - 1.0f));
}

/** The point at distance \a x along the path, moved \a up and to the \a side. */
static float3 point_at(const Path &path, float x, const float up, const float side)
{
  const float total = path.total();
  if (path.closed && total > 0.0f) {
    x = std::fmod(x, total);
    if (x < 0.0f) {
      x += total;
    }
  }
  else {
    x = std::clamp(x, 0.0f, total);
  }
  const int m = path.segments();
  int i = int(std::upper_bound(path.dist.begin() + 1, path.dist.end(), x) -
              (path.dist.begin() + 1));
  i = std::clamp(i, 0, m - 1);
  const float seg = path.dist[i + 1] - path.dist[i];
  const float s = seg > 0.0f ? std::clamp((x - path.dist[i]) / seg, 0.0f, 1.0f) : 0.0f;
  const int j = (i + 1) % path.points.size();
  return math::interpolate(path.points[i], path.points[j], s) +
         math::interpolate(offset_at(path, i, up, side), offset_at(path, j, up, side), s);
}

/** One copy: which pattern, and the stretch of the path it covers. */
struct Placement {
  int pattern;
  float start;
  float end;
};

static Mesh *place_copy(const Pattern &pattern,
                        const Path &path,
                        const Placement &placement,
                        const TrimModifierData &tmd)
{
  const float range = placement.end - placement.start;
  if (range <= 1e-7f) {
    return nullptr;
  }
  Mesh *copy = BKE_mesh_copy_for_eval(*pattern.mesh);
  MutableSpan<float3> positions = copy->vert_positions_for_write();
  const Span<float3> coords = pattern.coords;
  const float stretch = range / pattern.length;

  if (tmd.deform == MOD_TRIM_DEFORM_RIGID) {
    const float3 a = point_at(path, placement.start, 0.0f, 0.0f);
    const float3 b = point_at(path, placement.end, 0.0f, 0.0f);
    const float mid = (placement.start + placement.end) * 0.5f;
    const float3 middle = point_at(path, mid, 0.0f, 0.0f);
    float3 along = b - a;
    const float chord = math::length(along);
    along = chord > 1e-7f ?
                along / chord :
                math::normalize(point_at(path, mid + 1e-3f, 0.0f, 0.0f) - middle);
    const float3 up = any_perpendicular(along, point_at(path, mid, 1.0f, 0.0f) - middle);
    const float3 side = math::cross(along, up);
    const float3 base = math::midpoint(a, b);
    const float scale = (chord > 1e-7f ? chord : range) / pattern.length;
    const float center = pattern.min_along + pattern.length * 0.5f;
    for (const int v : positions.index_range()) {
      const float3 &c = coords[v];
      positions[v] = base + along * ((c.x - center) * scale) + up * c.y +
                     side * (c.z + tmd.offset);
    }
  }
  else {
    for (const int v : positions.index_range()) {
      const float3 &c = coords[v];
      const float x = placement.start + (c.x - pattern.min_along) * stretch;
      positions[v] = point_at(path, x, c.y, c.z + tmd.offset);
    }
  }
  copy->tag_positions_changed();
  return copy;
}

static void modify_geometry_set(ModifierData *md,
                                const ModifierEvalContext *ctx,
                                bke::GeometrySet *geometry_set)
{
  const TrimModifierData &tmd = *reinterpret_cast<const TrimModifierData *>(md);
  const Mesh *mesh = geometry_set->get_mesh();
  if (mesh == nullptr) {
    return;
  }
  const int along_axis = std::clamp(tmd.length_axis, 0, 2);
  const int up_axis = std::clamp(tmd.up_axis, 0, 2);
  if (along_axis == up_axis) {
    BKE_modifier_set_error(ctx->object, md, "Length Axis and Up Axis must differ");
    return;
  }

  /* The patterns to repeat, then the start and end objects. */
  Vector<Pattern> patterns;
  if (tmd.collection != nullptr) {
    Vector<Object *> objects;
    FOREACH_COLLECTION_OBJECT_RECURSIVE_BEGIN (tmd.collection, ob) {
      objects.append(ob);
    }
    FOREACH_COLLECTION_OBJECT_RECURSIVE_END;
    std::sort(objects.begin(), objects.end(), [](const Object *a, const Object *b) {
      return std::strcmp(a->id.name, b->id.name) < 0;
    });
    for (Object *ob : objects) {
      Pattern pattern;
      if (make_pattern(ob, ctx->object, tmd, along_axis, up_axis, pattern)) {
        patterns.append(std::move(pattern));
      }
    }
  }
  else {
    Pattern pattern;
    if (make_pattern(tmd.object, ctx->object, tmd, along_axis, up_axis, pattern)) {
      patterns.append(std::move(pattern));
    }
  }
  const int fill_num = patterns.size();
  if (fill_num == 0) {
    BKE_modifier_set_warning(ctx->object, md, "The pattern has no mesh");
    return;
  }
  int start_index = -1, end_index = -1;
  {
    Pattern pattern;
    if (make_pattern(tmd.start_object, ctx->object, tmd, along_axis, up_axis, pattern)) {
      start_index = int(patterns.append_and_get_index(std::move(pattern)));
    }
  }
  {
    Pattern pattern;
    if (make_pattern(tmd.end_object, ctx->object, tmd, along_axis, up_axis, pattern)) {
      end_index = int(patterns.append_and_get_index(std::move(pattern)));
    }
  }

  Vector<Path> paths = find_paths(*mesh, tmd.up_mode == MOD_TRIM_UP_FACES);
  if (paths.is_empty()) {
    BKE_modifier_set_warning(ctx->object, md, "No loose edges or open borders to follow");
  }

  int counter = 0;
  auto pick = [&]() {
    const int k = counter++;
    if (tmd.order == MOD_TRIM_ORDER_RANDOM && fill_num > 1) {
      return int(noise::hash(uint32_t(k), uint32_t(tmd.seed)) % uint32_t(fill_num));
    }
    return k % fill_num;
  };

  Vector<Mesh *> copies;
  bool too_many = false;
  for (Path &path : paths) {
    compute_frames(path);
    const float total = path.total();
    if (total <= 1e-7f) {
      continue;
    }
    Vector<Placement> placements;
    if (tmd.fit == MOD_TRIM_FIT_EDGE) {
      const int m = path.segments();
      for (const int i : IndexRange(m)) {
        int index;
        if (!path.closed && i == 0 && start_index >= 0) {
          index = start_index;
        }
        else if (!path.closed && i == m - 1 && end_index >= 0) {
          index = end_index;
        }
        else {
          index = pick();
        }
        const float a = path.dist[i], b = path.dist[i + 1];
        const float gap = std::min(tmd.gap, (b - a) * 0.9f) * 0.5f;
        placements.append({index, a + gap, b - gap});
      }
    }
    else {
      float from = 0.0f, to = total;
      bool fill = true;
      if (!path.closed && (start_index >= 0 || end_index >= 0)) {
        const float start_len = start_index >= 0 ? patterns[start_index].length : 0.0f;
        const float end_len = end_index >= 0 ? patterns[end_index].length : 0.0f;
        float fit = 1.0f;
        if (start_len + end_len >= total) {
          /* No room for anything else: squeeze the ends to fit. */
          fit = total / (start_len + end_len);
          fill = false;
        }
        if (start_index >= 0) {
          placements.append({start_index, 0.0f, start_len * fit});
          from = start_len * fit;
        }
        if (end_index >= 0) {
          placements.append({end_index, total - end_len * fit, total});
          to = total - end_len * fit;
        }
      }
      /* Open paths have no gap after the last copy. */
      const float room = path.closed ? to - from : to - from + tmd.gap;
      if (fill && room > 1e-6f) {
        Vector<int> picks;
        float sum = 0.0f;
        while (picks.size() < MAX_COPIES) {
          const int saved = counter;
          const int index = pick();
          const float unit = std::max(patterns[index].length + tmd.gap,
                                      patterns[index].length * 0.05f);
          if (!picks.is_empty() && sum + unit * 0.5f > room) {
            counter = saved;
            break;
          }
          picks.append(index);
          sum += unit;
        }
        const float fit = room / sum;
        float cursor = from;
        for (const int index : picks) {
          const float len = patterns[index].length;
          placements.append({index, cursor, cursor + len * fit});
          cursor += std::max(len + tmd.gap, len * 0.05f) * fit;
        }
      }
    }
    for (const Placement &placement : placements) {
      if (copies.size() >= MAX_COPIES) {
        too_many = true;
        break;
      }
      if (Mesh *copy = place_copy(patterns[placement.pattern], path, placement, tmd)) {
        copies.append(copy);
      }
    }
  }
  if (too_many) {
    BKE_modifier_set_warning(ctx->object, md, "Stopped at %d copies", MAX_COPIES);
  }

  std::unique_ptr<bke::Instances> instances = std::make_unique<bke::Instances>(copies.size());
  MutableSpan<int> handles = instances->reference_handles_for_write();
  MutableSpan<float4x4> transforms = instances->transforms_for_write();
  for (const int i : copies.index_range()) {
    handles[i] = instances->add_new_reference(
        bke::InstanceReference(bke::GeometrySet::from_mesh(copies[i])));
    transforms[i] = float4x4::identity();
  }

  bke::GeometrySet result;
  if (tmd.flag & MOD_TRIM_KEEP_EDGES) {
    result.replace_mesh(BKE_mesh_copy_for_eval(*mesh));
  }
  result.replace_instances(instances.release());
  result = geometry::realize_instances(result, geometry::RealizeInstancesOptions()).geometry;

  if ((tmd.flag & MOD_TRIM_MERGE) && result.has_mesh()) {
    const Mesh &joined = *result.get_mesh();
    if (std::optional<Mesh *> merged = geometry::mesh_merge_by_distance_all(
            joined, IndexMask(joined.verts_num), std::max(tmd.merge_distance, 1e-7f)))
    {
      result.replace_mesh(*merged);
    }
  }
  *geometry_set = std::move(result);
}

static void panel_draw(const bContext * /*C*/, Panel *panel)
{
  ui::Layout &layout = *panel->layout;
  PointerRNA ob_ptr;
  PointerRNA *ptr = modifier_panel_get_property_pointers(panel, &ob_ptr);

  layout.use_property_split_set(true);
  layout.prop(ptr, "object", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "collection", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  PointerRNA collection_ptr = RNA_pointer_get(ptr, "collection");
  if (!RNA_pointer_is_null(&collection_ptr)) {
    layout.prop(ptr, "order", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    if (RNA_enum_get(ptr, "order") == MOD_TRIM_ORDER_RANDOM) {
      layout.prop(ptr, "seed", UI_ITEM_NONE, std::nullopt, ICON_NONE);
    }
  }

  layout.separator();
  layout.prop(ptr, "fit", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.prop(ptr, "deform", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.prop(ptr, "length_axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.prop(ptr, "up_axis", ui::ITEM_R_EXPAND, std::nullopt, ICON_NONE);
  layout.prop(ptr, "up_mode", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.separator();
  ui::Layout &col = layout.column(true);
  col.prop(ptr, "scale", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "height", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "roll", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "gap", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  col.prop(ptr, "offset", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.separator();
  layout.prop(ptr, "start_object", UI_ITEM_NONE, std::nullopt, ICON_NONE);
  layout.prop(ptr, "end_object", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  layout.separator();
  ui::Layout &row = layout.row(true, IFACE_("Merge"));
  row.prop(ptr, "use_merge", UI_ITEM_NONE, "", ICON_NONE);
  ui::Layout &sub = row.row(true);
  sub.active_set(RNA_boolean_get(ptr, "use_merge"));
  sub.prop(ptr, "merge_distance", UI_ITEM_NONE, "", ICON_NONE);
  layout.prop(ptr, "use_keep_edges", UI_ITEM_NONE, std::nullopt, ICON_NONE);

  modifier_error_message_draw(layout, ptr);
}

static void panel_register(ARegionType *region_type)
{
  modifier_panel_register(region_type, eModifierType_Trim, panel_draw);
}

ModifierTypeInfo modifierType_Trim = {
    /*idname*/ "Trim",
    /*name*/ N_("Trim"),
    /*struct_name*/ "TrimModifierData",
    /*struct_size*/ sizeof(TrimModifierData),
    /*srna*/ &RNA_TrimModifier,
    /*type*/ ModifierTypeType::Constructive,
    /*flags*/ eModifierTypeFlag_AcceptsMesh | eModifierTypeFlag_SupportsEditmode |
        eModifierTypeFlag_EnableInEditmode,
    /*icon*/ ICON_MOD_TRIM,
    /*copy_data*/ BKE_modifier_copydata_generic,
    /*deform_verts*/ nullptr,
    /*deform_matrices*/ nullptr,
    /*deform_verts_EM*/ nullptr,
    /*deform_matrices_EM*/ nullptr,
    /*modify_mesh*/ nullptr,
    /*modify_geometry_set*/ modify_geometry_set,
    /*init_data*/ init_data,
    /*required_data_mask*/ nullptr,
    /*free_data*/ nullptr,
    /*is_disabled*/ is_disabled,
    /*update_depsgraph*/ update_depsgraph,
    /*depends_on_time*/ nullptr,
    /*depends_on_normals*/ nullptr,
    /*foreach_ID_link*/ foreach_ID_link,
    /*foreach_tex_link*/ nullptr,
    /*free_runtime_data*/ nullptr,
    /*panel_register*/ panel_register,
    /*blend_write*/ nullptr,
    /*blend_read*/ nullptr,
    /*foreach_cache*/ nullptr,
    /*foreach_working_space_color*/ nullptr,
};

}  // namespace blender

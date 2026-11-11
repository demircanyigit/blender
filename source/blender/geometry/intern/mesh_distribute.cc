/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 *
 * Distribute: objects packed on a surface by their measured sides.
 *
 * Each object is a box: its bounding box, standing on the surface. A new object is tried at a
 * few spots, most of them right next to an object already placed, the rest anywhere. A spot is
 * kept only if the box, grown by half the gap, touches no other box and stays on the surface.
 * Of the spots that fit, the one closest to its neighbors wins, so the objects pack tightly.
 * When no spot is left next to any object, the filler objects fill what is left.
 */

#include <algorithm>
#include <cmath>
#include <memory>

#include "BLI_array.hh"
#include "BLI_bounds.hh"
#include "BLI_index_mask.hh"
#include "BLI_map.hh"
#include "BLI_math_base.h"
#include "BLI_math_color.h"
#include "BLI_math_matrix.hh"
#include "BLI_math_vector.hh"
#include "BLI_memory_counter.hh"
#include "BLI_noise.hh"
#include "BLI_rand.hh"
#include "BLI_vector.hh"

#include "DNA_mesh_types.h"

#include "BKE_attribute.hh"
#include "BKE_bvhutils.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.hh"
#include "BKE_mesh_remesh_voxel.hh"

#include "GEO_isotropic_remesh.hh"
#include "GEO_mesh_copy_selection.hh"
#include "GEO_mesh_distribute.hh"
#include "GEO_mesh_divide.hh"
#include "GEO_mesh_sim_cache.hh"

namespace blender::geometry {

namespace distribute {

/** A placed object, as a box: center, axes, and half its size along each axis. */
struct Box {
  float3 center;
  float3 axis[3];
  float3 half;
  float radius;
};

/**
 * The largest gap between two boxes along any axis that separates them. Positive when they do
 * not touch.
 */
static float box_separation(const Box &a, const Box &b)
{
  const float3 t = b.center - a.center;
  float best = -1e30f;
  auto test = [&](const float3 &l) {
    const float len = math::length(l);
    if (len < 1e-6f) {
      return;
    }
    const float3 n = l / len;
    float ra = 0.0f, rb = 0.0f;
    for (int i = 0; i < 3; i++) {
      ra += a.half[i] * std::abs(math::dot(a.axis[i], n));
      rb += b.half[i] * std::abs(math::dot(b.axis[i], n));
    }
    best = std::max(best, std::abs(math::dot(t, n)) - ra - rb);
  };
  for (int i = 0; i < 3; i++) {
    test(a.axis[i]);
    test(b.axis[i]);
  }
  for (int i = 0; i < 3; i++) {
    for (int j = 0; j < 3; j++) {
      test(math::cross(a.axis[i], b.axis[j]));
    }
  }
  return best;
}

/** Boxes by grid cell, to find the ones near a spot quickly. */
struct BoxGrid {
  float cell;
  Map<int3, Vector<int>> cells;

  int3 key(const float3 &p) const
  {
    return int3(int(std::floor(p.x / cell)), int(std::floor(p.y / cell)), int(std::floor(p.z / cell)));
  }
  void add(const Box &box, const int index)
  {
    cells.lookup_or_add_default(key(box.center)).append(index);
  }
  template<typename Fn> void around(const float3 &p, const float reach, const Fn &fn) const
  {
    const int r = int(std::ceil(reach / cell));
    const int3 k = key(p);
    for (int z = -r; z <= r; z++) {
      for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
          if (const Vector<int> *list = cells.lookup_ptr(k + int3(x, y, z))) {
            for (const int i : *list) {
              fn(i);
            }
          }
        }
      }
    }
  }
};

}  // namespace distribute

static Vector<DistributePlacement> distribute_by_size_compute(const Mesh &surface,
                                                              const DistributeParams &params)
{
  using namespace distribute;
  Vector<DistributePlacement> result;
  if (params.items.is_empty() || surface.faces_num == 0) {
    return result;
  }

  /* The triangles to sample, by area. */
  const Span<float3> positions = surface.vert_positions();
  const Span<int3> tris = surface.corner_tris();
  const Span<int> tri_faces = surface.corner_tri_faces();
  const Span<int> corner_verts = surface.corner_verts();
  const bool use_selection = !params.selection.is_empty();
  Vector<int> tri_list;
  Vector<float> tri_area;
  float total_area = 0.0f;
  for (const int t : tris.index_range()) {
    if (use_selection && !params.selection[tri_faces[t]]) {
      continue;
    }
    const float3 &a = positions[corner_verts[tris[t][0]]];
    const float3 &b = positions[corner_verts[tris[t][1]]];
    const float3 &c = positions[corner_verts[tris[t][2]]];
    const float area = math::length(math::cross(b - a, c - a)) * 0.5f;
    if (area <= 0.0f) {
      continue;
    }
    total_area += area;
    tri_list.append(t);
    tri_area.append(total_area);
  }
  if (tri_list.is_empty()) {
    return result;
  }

  /* The surface, to snap spots onto and to keep objects on it. */
  IndexMaskMemory memory;
  const IndexMask face_mask = use_selection ?
                                  IndexMask::from_bools(params.selection, memory) :
                                  IndexMask(surface.faces_num);
  const bke::BVHTreeFromMesh bvh = bke::bvhtree_from_mesh_tris_init(surface, face_mask);
  if (bvh.tree == nullptr) {
    return result;
  }
  auto nearest = [&](const float3 &p, float3 &r_co, float3 &r_no) {
    BVHTreeNearest hit;
    hit.index = -1;
    hit.dist_sq = 1e30f;
    BLI_bvhtree_find_nearest(bvh.tree, p, &hit, bvh.nearest_callback, (void *)&bvh);
    if (hit.index < 0) {
      return false;
    }
    r_co = float3(hit.co);
    r_no = math::normalize(float3(hit.no));
    return true;
  };

  RandomNumberGenerator rng(uint32_t(params.seed) * 2654435761u + 17u);
  auto random_spot = [&](float3 &r_co, float3 &r_no) {
    const float r = rng.get_float() * total_area;
    const int i = std::min(int(std::lower_bound(tri_area.begin(), tri_area.end(), r) -
                               tri_area.begin()),
                           int(tri_list.size()) - 1);
    const int3 &tri = tris[tri_list[i]];
    const float3 &a = positions[corner_verts[tri[0]]];
    const float3 &b = positions[corner_verts[tri[1]]];
    const float3 &c = positions[corner_verts[tri[2]]];
    float u = rng.get_float(), v = rng.get_float();
    if (u + v > 1.0f) {
      u = 1.0f - u;
      v = 1.0f - v;
    }
    r_co = a + (b - a) * u + (c - a) * v;
    r_no = math::normalize(math::cross(b - a, c - a));
  };

  /* The way objects face, at a spot. */
  auto frame = [&](const float3 &normal, float3 &r_x, float3 &r_y, float3 &r_z) {
    r_z = params.align_to_normal ? normal : float3(0.0f, 0.0f, 1.0f);
    float3 along;
    switch (params.direction) {
      case DistributeDirection::WorldX:
        along = float3(1.0f, 0.0f, 0.0f);
        break;
      case DistributeDirection::WorldY:
        along = float3(0.0f, 1.0f, 0.0f);
        break;
      case DistributeDirection::Auto:
      default:
        along = std::abs(r_z.z) < 0.7f ? math::cross(float3(0.0f, 0.0f, 1.0f), r_z) :
                                         float3(1.0f, 0.0f, 0.0f);
        break;
    }
    along -= r_z * math::dot(along, r_z);
    if (math::length_squared(along) < 1e-8f) {
      along = float3(0.0f, 1.0f, 0.0f) - r_z * r_z.y;
    }
    r_x = math::normalize(along);
    float angle = 0.0f;
    switch (params.rotation) {
      case DistributeRotation::Align:
        angle = params.jitter * (rng.get_float() * 2.0f - 1.0f);
        break;
      case DistributeRotation::Steps:
        angle = float(M_PI_2) * float(rng.get_int32(4)) +
                params.jitter * (rng.get_float() * 2.0f - 1.0f);
        break;
      case DistributeRotation::Random:
        angle = float(M_PI) * 2.0f * rng.get_float();
        break;
    }
    const float3 y0 = math::cross(r_z, r_x);
    r_x = r_x * std::cos(angle) + y0 * std::sin(angle);
    r_y = math::cross(r_z, r_x);
  };

  Vector<int> main_items, filler_items;
  float max_size = 0.0f;
  for (const int i : params.items.index_range()) {
    const DistributeItem &item = params.items[i];
    (item.filler ? filler_items : main_items).append(i);
    max_size = std::max(max_size, math::length(item.max - item.min));
  }
  if (main_items.is_empty()) {
    main_items = filler_items;
    filler_items.clear();
  }
  const float scale_hi = std::max({params.scale_min, params.scale_max, 1e-4f});
  const float gap = std::max(params.gap, 0.0f);
  BoxGrid grid;
  grid.cell = std::max(max_size * scale_hi + gap, 1e-4f);
  const float reach = grid.cell * 1.5f;

  Vector<Box> boxes;
  Vector<bool> open;
  Vector<int> fails;
  Vector<int> open_list;

  const int max_count = std::max(params.max_count, 0);
  const int tries = std::clamp(params.tries, 1, 1000);

  /* One packing round with these objects at this size. */
  auto pack = [&](const Span<int> items, const float size_factor, const bool filler) {
    open_list.clear();
    for (const int i : boxes.index_range()) {
      open[i] = true;
      fails[i] = 0;
      open_list.append(i);
    }
    int misses = 0;
    while (int(result.size()) < max_count) {
      const int item_index = items[rng.get_int32(int(items.size()))];
      const DistributeItem &item = params.items[item_index];
      const float scale = math::interpolate(params.scale_min, params.scale_max, rng.get_float()) *
                          size_factor;
      if (scale <= 0.0f) {
        break;
      }
      const float3 size = (item.max - item.min) * scale;
      const float3 half(size.x * 0.5f + gap * 0.5f, size.y * 0.5f + gap * 0.5f, size.z * 0.5f);

      /* Grow from one object that still has room around it, or start somewhere new. */
      int from = -1;
      while (!open_list.is_empty() && from < 0) {
        const int k = rng.get_int32(int(open_list.size()));
        const int b = open_list[k];
        if (open[b]) {
          from = b;
        }
        else {
          open_list.remove_and_reorder(k);
        }
      }

      float best_score = 1e30f;
      Box best_box;
      float3 best_x, best_y, best_z, best_spot;
      for (int t = 0; t < tries; t++) {
        float3 spot, normal;
        if (from >= 0 && t < tries * 3 / 4) {
          /* Right next to the object, in a random direction along the surface. */
          const Box &f = boxes[from];
          const float a = float(M_PI) * 2.0f * rng.get_float();
          const float3 dir = f.axis[0] * std::cos(a) + f.axis[1] * std::sin(a);
          const float ra = std::abs(std::cos(a)) * f.half.x + std::abs(std::sin(a)) * f.half.y;
          const float rb = std::max(half.x, half.y) * (0.6f + 0.4f * rng.get_float());
          const float3 guess = f.center - f.axis[2] * f.half.z + dir * (ra + rb);
          if (!nearest(guess, spot, normal)) {
            continue;
          }
        }
        else {
          random_spot(spot, normal);
        }
        float3 x, y, z;
        frame(normal, x, y, z);
        Box box;
        box.axis[0] = x;
        box.axis[1] = y;
        box.axis[2] = z;
        box.half = half;
        box.center = spot + z * (params.offset + half.z);
        box.radius = math::length(half);

        /* It must stay on the surface: its bottom corners close to it. */
        if (params.stay_inside) {
          bool off = false;
          const float tolerance = std::max(std::min(half.x, half.y) * 0.25f, gap);
          for (const float sx : {-1.0f, 1.0f}) {
            for (const float sy : {-1.0f, 1.0f}) {
              const float3 corner = spot + x * ((half.x - gap * 0.5f) * sx) +
                                    y * ((half.y - gap * 0.5f) * sy);
              float3 co, no;
              if (!nearest(corner, co, no) || math::distance(co, corner) > tolerance) {
                off = true;
              }
            }
          }
          if (off) {
            continue;
          }
        }

        /* It must touch nothing. The closer it comes to its neighbors, the better. */
        bool hit = false;
        float closest = 1e20f;
        grid.around(box.center, box.radius + reach, [&](const int other) {
          if (hit) {
            return;
          }
          const Box &o = boxes[other];
          if (math::distance(o.center, box.center) > o.radius + box.radius + reach) {
            return;
          }
          const float sep = box_separation(box, o);
          if (sep < 0.0f) {
            hit = true;
          }
          else {
            closest = std::min(closest, sep);
          }
        });
        if (hit) {
          continue;
        }
        const float score = closest + rng.get_float() * gap * 0.25f;
        if (score < best_score) {
          best_score = score;
          best_box = box;
          best_x = x;
          best_y = y;
          best_z = z;
          best_spot = spot;
        }
      }

      if (best_score >= 1e29f) {
        if (from >= 0) {
          /* After a few objects did not fit, there is no room left around it. */
          if (++fails[from] >= 3) {
            open[from] = false;
          }
          continue;
        }
        /* Nowhere next to an object, and nothing found anywhere either. */
        if (++misses > 4) {
          break;
        }
        continue;
      }
      misses = 0;

      const int index = boxes.size();
      boxes.append(best_box);
      open.append(true);
      fails.append(0);
      open_list.append(index);
      grid.add(best_box, index);

      /* The object's origin, so its box sits centered on the spot. */
      const float3x3 rot(best_x, best_y, best_z);
      const float3 c = (item.min + item.max) * 0.5f;
      const float3 local(c.x, c.y, item.min.z);
      const float3 origin = best_spot + best_z * params.offset - rot * (local * scale);
      float4x4 transform = float4x4(float3x3(rot) * math::from_scale<float3x3>(float3(scale)));
      transform.location() = origin;
      result.append({item_index, transform, filler});
    }
  };

  pack(main_items, 1.0f, false);
  if (params.fillers) {
    if (!filler_items.is_empty()) {
      pack(filler_items, 1.0f, true);
    }
    else {
      const float f = std::clamp(params.filler_scale, 0.05f, 1.0f);
      pack(main_items, f, true);
      pack(main_items, f * f, true);
    }
  }
  return result;
}

/* -------------------------------------------------------------------- */
/** \name Mortar
 * \{ */

static Mesh *distribute_mortar_compute(const Mesh &surface,
                                       const DistributeMortarParams &params,
                                       std::string &r_message)
{
  struct MeshFree {
    void operator()(Mesh *mesh) const
    {
      if (mesh != nullptr) {
        BKE_id_free(nullptr, mesh);
      }
    }
  };
  using OwnedMesh = std::unique_ptr<Mesh, MeshFree>;

  OwnedMesh base;
  if (!params.selection.is_empty()) {
    const std::optional<Mesh *> copy = mesh_copy_selection(
        surface, VArray<bool>::from_span(params.selection), bke::AttrDomain::Face);
    if (copy.has_value()) {
      if (*copy == nullptr) {
        return nullptr;
      }
      base = OwnedMesh(*copy);
    }
  }
  if (!base) {
    base = OwnedMesh(BKE_mesh_copy_for_eval(surface));
  }
  if (base->faces_num == 0) {
    return nullptr;
  }

  /* Offset along the normals. */
  if (params.offset != 0.0f) {
    const Array<float3> normals(base->vert_normals());
    MutableSpan<float3> positions = base->vert_positions_for_write();
    for (const int i : positions.index_range()) {
      positions[i] += normals[i] * params.offset;
    }
    base->tag_positions_changed();
  }

  /* Dense enough for the bumps. */
  const Bounds<float3> bounds = *bounds::min_max(base->vert_positions());
  const float extent = math::reduce_max(bounds.max - bounds.min);
  OwnedMesh mesh;
  switch (params.remesh) {
    case StoneworkRemesh::Isotropic: {
      IsotropicRemeshParams remesh;
      remesh.target_edge_length = std::max(params.resolution, extent / 1000.0f);
      remesh.preserve_boundary = true;
      remesh.preserve_sharp_edges = true;
      remesh.sharp_angle = 0.5235988f;
      remesh.transfer_attributes = true;
      mesh = OwnedMesh(isotropic_remesh(*base, remesh));
      break;
    }
    case StoneworkRemesh::Voxel:
      mesh = OwnedMesh(BKE_mesh_remesh_voxel(
          base.get(), std::max(params.resolution, extent / 1500.0f), 0.0f, 0.0f, nullptr));
      break;
    case StoneworkRemesh::Divide: {
      MeshDivideParams divide;
      divide.size = float3(std::max(params.resolution, extent / 1000.0f));
      std::string message;
      mesh = OwnedMesh(mesh_divide(*base, divide, message));
      if (!message.empty()) {
        r_message += message + " ";
      }
      break;
    }
    case StoneworkRemesh::None:
      break;
  }
  if (!mesh) {
    mesh = std::move(base);
  }

  /* Bumps along the normals. */
  if (params.noise_strength != 0.0f) {
    const Array<float3> normals(mesh->vert_normals());
    MutableSpan<float3> positions = mesh->vert_positions_for_write();
    const float3 shift(float(params.seed % 1000) * 1.37f, float(params.seed % 977) * 0.71f, 3.3f);
    for (const int i : positions.index_range()) {
      const float n = noise::perlin_fbm<float3>(positions[i] * params.noise_scale + shift,
                                                params.noise_detail,
                                                0.5f,
                                                2.0f,
                                                true);
      positions[i] += normals[i] * ((n - 0.5f) * 2.0f * params.noise_strength);
    }
    mesh->tag_positions_changed();
  }

  bke::MutableAttributeAccessor attributes = mesh->attributes_for_write();
  attributes.remove("material_index");
  if (params.material_index > 0) {
    bke::SpanAttributeWriter<int> material = attributes.lookup_or_add_for_write_only_span<int>(
        "material_index", bke::AttrDomain::Face);
    material.span.fill(params.material_index);
    material.finish();
  }
  return mesh.release();
}

/** \} */

float4 distribute_random_color(const int index,
                               const int seed,
                               const StoneworkColorMode mode,
                               const float2 hue,
                               const float2 saturation,
                               const float2 value)
{
  auto hash01 = [&](const uint32_t c) {
    return float(noise::hash(uint32_t(index), uint32_t(seed), c)) / 4294967295.0f;
  };
  const float v = math::interpolate(value.x, value.y, hash01(13u));
  if (mode == StoneworkColorMode::Gray) {
    return float4(v, v, v, 1.0f);
  }
  float h = math::interpolate(hue.x, hue.y, hash01(11u));
  h -= std::floor(h);
  float4 col(0.0f, 0.0f, 0.0f, 1.0f);
  hsv_to_rgb(h, math::interpolate(saturation.x, saturation.y, hash01(12u)), v, &col.x, &col.y, &col.z);
  return col;
}

/* Objects are evaluated again for undo and for changes to other objects. The results are taken
 * from the cache when nothing they depend on changed, so the packing and the mortar are not made
 * all over again. */

namespace {

class PlacementsCachedValue : public memory_cache::CachedValue {
 public:
  Vector<DistributePlacement> placements;

  void count_memory(MemoryCounter &memory) const override
  {
    memory.add(placements.as_span().size_in_bytes());
  }
};

class MortarCachedValue : public memory_cache::CachedValue {
 public:
  Mesh *mesh = nullptr;
  std::string message;

  ~MortarCachedValue() override
  {
    if (mesh != nullptr) {
      BKE_id_free(nullptr, mesh);
    }
  }

  void count_memory(MemoryCounter &memory) const override
  {
    if (mesh != nullptr) {
      mesh->count_memory(memory);
    }
  }
};

}  // namespace

Vector<DistributePlacement> distribute_by_size(const Mesh &surface,
                                               const DistributeParams &params)
{
  SimCacheHasher hasher("distribute_by_size");
  hasher.add_mesh_shape(surface);
  hasher.add(params.items.size());
  for (const DistributeItem &item : params.items) {
    hasher.add(item.min);
    hasher.add(item.max);
    hasher.add(item.filler);
  }
  hasher.add_span(params.selection);
  hasher.add(params.gap);
  hasher.add(params.tries);
  hasher.add(params.align_to_normal);
  hasher.add(params.direction);
  hasher.add(params.rotation);
  hasher.add(params.jitter);
  hasher.add(params.scale_min);
  hasher.add(params.scale_max);
  hasher.add(params.offset);
  hasher.add(params.stay_inside);
  hasher.add(params.fillers);
  hasher.add(params.filler_scale);
  hasher.add(params.max_count);
  hasher.add(params.seed);
  const std::shared_ptr<const PlacementsCachedValue> value =
      std::dynamic_pointer_cast<const PlacementsCachedValue>(
          sim_cache_get_value(hasher.finish(), [&]() {
            auto value = std::make_unique<PlacementsCachedValue>();
            value->placements = distribute_by_size_compute(surface, params);
            return value;
          }));
  if (!value) {
    return {};
  }
  return value->placements;
}

Mesh *distribute_mortar(const Mesh &surface,
                        const DistributeMortarParams &params,
                        std::string &r_message)
{
  SimCacheHasher hasher("distribute_mortar");
  hasher.add_mesh(surface);
  hasher.add(params.offset);
  hasher.add(params.remesh);
  hasher.add(params.resolution);
  hasher.add(params.noise_strength);
  hasher.add(params.noise_scale);
  hasher.add(params.noise_detail);
  hasher.add(params.seed);
  hasher.add(params.material_index);
  hasher.add_span(params.selection);
  const std::shared_ptr<const MortarCachedValue> value =
      std::dynamic_pointer_cast<const MortarCachedValue>(
          sim_cache_get_value(hasher.finish(), [&]() {
            auto value = std::make_unique<MortarCachedValue>();
            value->mesh = distribute_mortar_compute(surface, params, value->message);
            return value;
          }));
  if (!value) {
    return nullptr;
  }
  r_message = value->message;
  if (value->mesh == nullptr) {
    return nullptr;
  }
  return BKE_mesh_copy_for_eval(*value->mesh);
}

}  // namespace blender::geometry

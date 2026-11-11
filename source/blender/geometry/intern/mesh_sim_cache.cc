/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup geo
 */

#include <memory>

#include <xxhash.h>

#include "DNA_mesh_types.h"

#include "BLI_generic_key.hh"
#include "BLI_hash.hh"
#include "BLI_memory_cache.hh"
#include "BLI_memory_counter.hh"

#include "BKE_attribute.hh"
#include "BKE_lib_id.hh"
#include "BKE_mesh.h"
#include "BKE_mesh.hh"

#include "GEO_mesh_sim_cache.hh"

namespace blender::geometry {

SimCacheHasher::SimCacheHasher(const StringRef kind)
{
  XXH3_state_t *state = XXH3_createState();
  XXH3_128bits_reset(state);
  state_ = state;
  this->add_string(kind);
}

SimCacheHasher::~SimCacheHasher()
{
  XXH3_freeState(static_cast<XXH3_state_t *>(state_));
}

void SimCacheHasher::add_bytes(const void *data, const int64_t size)
{
  if (size > 0) {
    XXH3_128bits_update(static_cast<XXH3_state_t *>(state_), data, size_t(size));
  }
}

void SimCacheHasher::add_string(const StringRef str)
{
  this->add(str.size());
  this->add_bytes(str.data(), str.size());
}

void SimCacheHasher::add_mesh_shape(const Mesh &mesh)
{
  this->add(mesh.verts_num);
  this->add(mesh.faces_num);
  this->add_span(mesh.vert_positions());
  this->add_span(mesh.face_offsets());
  this->add_span(mesh.corner_verts());
}

void SimCacheHasher::add_mesh(const Mesh &mesh)
{
  this->add(mesh.verts_num);
  this->add(mesh.edges_num);
  this->add(mesh.faces_num);
  this->add(mesh.corners_num);
  this->add_span(mesh.face_offsets());
  mesh.attributes().foreach_attribute([&](const bke::AttributeIter &iter) {
    this->add_string(iter.name);
    this->add(iter.domain);
    this->add(iter.data_type);
    const bke::GAttributeReader reader = iter.get();
    if (!reader) {
      return;
    }
    const GVArraySpan data(*reader);
    if (data.type().is_trivial) {
      this->add_bytes(data.data(), data.size_in_bytes());
    }
  });
}

SimCacheKey SimCacheHasher::finish()
{
  const XXH128_hash_t hash = XXH3_128bits_digest(static_cast<XXH3_state_t *>(state_));
  return {hash.low64, hash.high64};
}

namespace {

class SimGenericKey : public GenericKey {
 public:
  SimCacheKey key;

  SimGenericKey(const SimCacheKey &key) : key(key) {}

  uint64_t hash() const override
  {
    return key.low ^ key.high;
  }

  bool equal_to(const GenericKey &other) const override
  {
    if (const auto *other_key = dynamic_cast<const SimGenericKey *>(&other)) {
      return key.low == other_key->key.low && key.high == other_key->key.high;
    }
    return false;
  }

  std::unique_ptr<GenericKey> to_storable() const override
  {
    return std::make_unique<SimGenericKey>(key);
  }
};

class SimCachedValue : public memory_cache::CachedValue {
 public:
  /** Null when the simulation failed. */
  Mesh *mesh = nullptr;
  const char *error = nullptr;

  ~SimCachedValue() override
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

Mesh *sim_cache_get(const SimCacheKey &key,
                    const FunctionRef<Mesh *(const char **r_error)> compute,
                    const char **r_error)
{
  *r_error = nullptr;
  const std::shared_ptr<const SimCachedValue> value = memory_cache::get<SimCachedValue>(
      SimGenericKey(key), [&]() {
        auto value = std::make_unique<SimCachedValue>();
        value->mesh = compute(&value->error);
        return value;
      });
  if (!value || value->mesh == nullptr) {
    *r_error = value ? value->error : nullptr;
    return nullptr;
  }
  /* The copy shares its arrays with the cached mesh, so it is cheap. */
  return BKE_mesh_copy_for_eval(*value->mesh);
}

std::shared_ptr<const memory_cache::CachedValue> sim_cache_get_value(
    const SimCacheKey &key, const FunctionRef<std::unique_ptr<memory_cache::CachedValue>()> compute)
{
  return memory_cache::get_base(SimGenericKey(key), compute);
}

}  // namespace blender::geometry

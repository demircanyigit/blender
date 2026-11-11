/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

#pragma once

/** \file
 * \ingroup geo
 *
 * Remembers the results of slow mesh simulations by the contents of their inputs. Blender
 * evaluates objects again for many reasons that don't change them, like undo or edits to other
 * objects, and then the result is taken from here instead of simulating again.
 */

#include <cstdint>
#include <memory>
#include <type_traits>

#include "BLI_function_ref.hh"
#include "BLI_memory_cache.hh"
#include "BLI_span.hh"
#include "BLI_string_ref.hh"

namespace blender {
struct Mesh;
}

namespace blender::geometry {

struct SimCacheKey {
  uint64_t low = 0;
  uint64_t high = 0;
};

/** Builds a #SimCacheKey from everything a simulation reads. */
class SimCacheHasher {
  void *state_;

 public:
  /** \param kind: Keeps the results of different simulations apart. */
  explicit SimCacheHasher(StringRef kind);
  ~SimCacheHasher();
  SimCacheHasher(const SimCacheHasher &) = delete;
  SimCacheHasher &operator=(const SimCacheHasher &) = delete;

  void add_bytes(const void *data, int64_t size);

  template<typename T> void add(const T &value)
  {
    static_assert(std::is_trivially_copyable_v<T>);
    this->add_bytes(&value, sizeof(T));
  }

  template<typename T> void add_span(const Span<T> values)
  {
    static_assert(std::is_trivially_copyable_v<T>);
    this->add(values.size());
    this->add_bytes(values.data(), values.size_in_bytes());
  }

  void add_string(StringRef str);

  /** Everything in the mesh: sizes, faces and all attributes, since the result carries them. */
  void add_mesh(const Mesh &mesh);

  /** Only the shape of the mesh: positions and faces. For meshes that are only collided with. */
  void add_mesh_shape(const Mesh &mesh);

  SimCacheKey finish();
};

/**
 * The result of \a compute for these inputs, from the cache when it was computed before.
 * \param compute: Runs the simulation. Returns null with an error message on failure.
 * \return A new mesh owned by the caller, or null with \a r_error set.
 */
Mesh *sim_cache_get(const SimCacheKey &key,
                    FunctionRef<Mesh *(const char **r_error)> compute,
                    const char **r_error);

/**
 * Like #sim_cache_get, for results that are more than one mesh. The value is shared with the
 * cache, so it must not be changed: copy what is needed out of it.
 */
std::shared_ptr<const memory_cache::CachedValue> sim_cache_get_value(
    const SimCacheKey &key, FunctionRef<std::unique_ptr<memory_cache::CachedValue>()> compute);

}  // namespace blender::geometry

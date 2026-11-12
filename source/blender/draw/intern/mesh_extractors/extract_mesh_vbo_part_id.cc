/* SPDX-FileCopyrightText: 2026 Blender Authors
 *
 * SPDX-License-Identifier: GPL-2.0-or-later */

/** \file
 * \ingroup draw
 *
 * For each corner, the loose part, the material index and the face set of its face. Workbench
 * uses them to draw outlines around parts of a mesh.
 */

#include "BLI_atomic_disjoint_set.hh"

#include "BLI_map.hh"

#include "BKE_attribute.hh"
#include "BKE_mesh.hh"
#include "BKE_paint.hh"

#include "draw_subdivision.hh"
#include "extract_mesh.hh"

namespace blender::draw {

static const GPUVertFormat &get_part_id_format()
{
  static const GPUVertFormat format = []() {
    GPUVertFormat format{};
    GPU_vertformat_attr_add(&format, "pid", gpu::VertAttrType::SINT_32_32_32);
    return format;
  }();
  return format;
}

/** Per face: loose part, material index and face set. */
static Array<int3> part_ids_per_face(const MeshRenderData &mr)
{
  Array<int3> ids(mr.faces_num);
  if (mr.extract_type == MeshExtractType::BMesh) {
    BMesh &bm = *mr.bm;
    AtomicDisjointSet parts(bm.totvert);
    BMIter iter;
    BMEdge *edge;
    BM_ITER_MESH (edge, &iter, &bm, BM_EDGES_OF_MESH) {
      parts.join(BM_elem_index_get(edge->v1), BM_elem_index_get(edge->v2));
    }
    const int face_set_offset = CustomData_get_offset_named(
        &bm.pdata, CD_PROP_INT32, ".sculpt_face_set");
    threading::parallel_for(IndexRange(bm.totface), 2048, [&](const IndexRange range) {
      for (const int face_index : range) {
        const BMFace &face = *BM_face_at_index(&bm, face_index);
        const int part = parts.find_root(BM_elem_index_get(BM_FACE_FIRST_LOOP(&face)->v));
        const int face_set = (face_set_offset != -1) ?
                                 BM_ELEM_CD_GET_INT(&face, face_set_offset) :
                                 mr.mesh->face_sets_color_default;
        ids[face_index] = int3(part, face.mat_nr, face_set);
      }
    });
    return ids;
  }

  AtomicDisjointSet parts(mr.verts_num);
  threading::parallel_for(mr.edges.index_range(), 4096, [&](const IndexRange range) {
    for (const int edge : range) {
      parts.join(mr.edges[edge][0], mr.edges[edge][1]);
    }
  });
  const bke::AttributeAccessor attributes = mr.mesh->attributes();
  const VArraySpan face_sets = *attributes.lookup<int>(".sculpt_face_set", bke::AttrDomain::Face);
  const int default_face_set = mr.mesh->face_sets_color_default;
  threading::parallel_for(IndexRange(mr.faces_num), 2048, [&](const IndexRange range) {
    for (const int face_index : range) {
      const int part = parts.find_root(mr.corner_verts[mr.faces[face_index].first()]);
      const int material = mr.material_indices.is_empty() ? 0 :
                                                            mr.material_indices[face_index];
      const int face_set = face_sets.is_empty() ? default_face_set : face_sets[face_index];
      ids[face_index] = int3(part, material, face_set);
    }
  });
  return ids;
}

gpu::VertBufPtr extract_part_ids(const MeshRenderData &mr)
{
  gpu::VertBufPtr vbo = gpu::VertBufPtr(GPU_vertbuf_create_with_format(get_part_id_format()));
  GPU_vertbuf_data_alloc(*vbo, mr.corners_num);
  MutableSpan<int3> vbo_data = vbo->data<int3>();

  const Array<int3> ids = part_ids_per_face(mr);
  if (mr.extract_type == MeshExtractType::BMesh) {
    BMesh &bm = *mr.bm;
    threading::parallel_for(IndexRange(bm.totface), 2048, [&](const IndexRange range) {
      for (const int face_index : range) {
        const BMFace &face = *BM_face_at_index(&bm, face_index);
        const IndexRange face_range(BM_elem_index_get(BM_FACE_FIRST_LOOP(&face)), face.len);
        vbo_data.slice(face_range).fill(ids[face_index]);
      }
    });
  }
  else {
    threading::parallel_for(IndexRange(mr.faces_num), 2048, [&](const IndexRange range) {
      for (const int face_index : range) {
        vbo_data.slice(mr.faces[face_index]).fill(ids[face_index]);
      }
    });
  }
  return vbo;
}

gpu::VertBufPtr extract_part_ids_subdiv(const MeshRenderData &mr,
                                        const DRWSubdivCache &subdiv_cache)
{
  const int subdiv_corners_num = subdiv_cache.num_subdiv_loops;
  gpu::VertBufPtr vbo = gpu::VertBufPtr(GPU_vertbuf_create_with_format(get_part_id_format()));
  GPU_vertbuf_data_alloc(*vbo, subdiv_full_vbo_size(mr, subdiv_cache));
  MutableSpan<int3> vbo_data = vbo->data<int3>();
  vbo_data.fill(int3(0));

  const Array<int3> ids = part_ids_per_face(mr);
  const Span<int> subdiv_loop_face_index(subdiv_cache.subdiv_loop_face_index, subdiv_corners_num);
  threading::parallel_for(IndexRange(subdiv_corners_num), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      vbo_data[i] = ids[subdiv_loop_face_index[i]];
    }
  });
  return vbo;
}

static const GPUVertFormat &get_island_color_format()
{
  static const GPUVertFormat format = []() {
    GPUVertFormat format{};
    GPU_vertformat_attr_add(&format, "icol", gpu::VertAttrType::UNORM_8_8_8_8);
    /* Read as the color attribute by the Workbench shaders. */
    GPU_vertformat_alias_add(&format, "ac");
    return format;
  }();
  return format;
}

/** Per face: a color for its loose part. Parts are numbered in face order, so the colors stay
 * the same when the mesh changes elsewhere. */
static Array<uchar4> island_colors_per_face(const MeshRenderData &mr)
{
  const Array<int3> ids = part_ids_per_face(mr);
  Map<int, int> part_numbers;
  Array<uchar4> colors(ids.size());
  for (const int face : ids.index_range()) {
    const int number = part_numbers.lookup_or_add(ids[face].x, part_numbers.size());
    uchar color[4];
    BKE_paint_face_set_overlay_color_get(number + 1, 0, color);
    colors[face] = uchar4(color[0], color[1], color[2], 255);
  }
  return colors;
}

gpu::VertBufPtr extract_island_colors(const MeshRenderData &mr)
{
  gpu::VertBufPtr vbo = gpu::VertBufPtr(
      GPU_vertbuf_create_with_format(get_island_color_format()));
  GPU_vertbuf_data_alloc(*vbo, mr.corners_num);
  MutableSpan<uchar4> vbo_data = vbo->data<uchar4>();

  const Array<uchar4> colors = island_colors_per_face(mr);
  if (mr.extract_type == MeshExtractType::BMesh) {
    BMesh &bm = *mr.bm;
    threading::parallel_for(IndexRange(bm.totface), 2048, [&](const IndexRange range) {
      for (const int face_index : range) {
        const BMFace &face = *BM_face_at_index(&bm, face_index);
        const IndexRange face_range(BM_elem_index_get(BM_FACE_FIRST_LOOP(&face)), face.len);
        vbo_data.slice(face_range).fill(colors[face_index]);
      }
    });
  }
  else {
    threading::parallel_for(IndexRange(mr.faces_num), 2048, [&](const IndexRange range) {
      for (const int face_index : range) {
        vbo_data.slice(mr.faces[face_index]).fill(colors[face_index]);
      }
    });
  }
  return vbo;
}

gpu::VertBufPtr extract_island_colors_subdiv(const MeshRenderData &mr,
                                             const DRWSubdivCache &subdiv_cache)
{
  const int subdiv_corners_num = subdiv_cache.num_subdiv_loops;
  gpu::VertBufPtr vbo = gpu::VertBufPtr(
      GPU_vertbuf_create_with_format(get_island_color_format()));
  GPU_vertbuf_data_alloc(*vbo, subdiv_full_vbo_size(mr, subdiv_cache));
  MutableSpan<uchar4> vbo_data = vbo->data<uchar4>();
  vbo_data.fill(uchar4(255));

  const Array<uchar4> colors = island_colors_per_face(mr);
  const Span<int> subdiv_loop_face_index(subdiv_cache.subdiv_loop_face_index, subdiv_corners_num);
  threading::parallel_for(IndexRange(subdiv_corners_num), 4096, [&](const IndexRange range) {
    for (const int i : range) {
      vbo_data[i] = colors[subdiv_loop_face_index[i]];
    }
  });
  return vbo;
}

}  // namespace blender::draw

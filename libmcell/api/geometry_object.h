/******************************************************************************
 *
 * Copyright (C) 2020 by
 * The Salk Institute for Biological Studies
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 * USA.
 *
******************************************************************************/

#ifndef API_GEOMETRY_OBJECT_H
#define API_GEOMETRY_OBJECT_H

#include "generated/gen_geometry_object.h"
#include "api/common.h"
#include "defines.h"

namespace MCell {
namespace API {

class GeometryObject: public GenGeometryObject {
public:
  GEOMETRY_OBJECT_CTOR()

  void postprocess_in_ctor() {
    node_type = RegionNodeType::LeafGeometryObject;
    partition_id = PARTITION_ID_INVALID;
  }

  void check_semantics() const override {
    for (auto& v: vertex_list) {
      if (v.size() != 3) {
        throw ValueError(
            "Each item in the 'vertex_list' argument must be a triplet of floats, error for " +
            vec_nonptr_to_str(v) + ".");
      }
    }

    for (auto& e: element_connections) {
      if (e.size() != 3) {
        throw ValueError(
            "Each item in the 'element_connections' argument must be a triplet of integers, error for " +
            vec_nonptr_to_str(e) + ".");
        for (int vertex_index: e) {
          if (vertex_index < 0 || vertex_index >= (int)vertex_list.size()) {
            throw ValueError(
                "Vertex index the 'element_connections' is out of range, error for " +
                std::to_string(vertex_index));
          }
        }
      }
    }
  }

  // simulation engine mapping
  partition_id_t partition_id;
  std::vector<vertex_index_t> vertex_indices; // vertex_list[i] has vertex index vertex_indices[i]
  std::vector<wall_index_t> wall_indices; // element_connections[i] has wall index wall_indices[i]
};

} // namespace API
} // namespace MCell

#endif // API_GEOMETRY_OBJECT_H

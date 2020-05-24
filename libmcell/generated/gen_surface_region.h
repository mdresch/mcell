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

#ifndef API_GEN_SURFACE_REGION_H
#define API_GEN_SURFACE_REGION_H

#include "../api/common.h"
#include "../api/base_data_class.h"
#include "../api/region.h"


namespace MCell {
namespace API {

class GeometryObject;
class Region;

#define SURFACE_REGION_CTOR() \
    SurfaceRegion( \
        const std::string& name_, \
        const std::vector<int> element_connections_, \
        std::shared_ptr<GeometryObject> parent_ = nullptr, \
        const RegionNodeType node_type_ = RegionNodeType::Unset, \
        std::shared_ptr<Region> left_node_ = nullptr, \
        std::shared_ptr<Region> right_node_ = nullptr \
    )  : GenSurfaceRegion(node_type_,left_node_,right_node_) { \
      class_name = "SurfaceRegion"; \
      name = name_; \
      element_connections = element_connections_; \
      parent = parent_; \
      node_type = node_type_; \
      left_node = left_node_; \
      right_node = right_node_; \
      postprocess_in_ctor();\
      check_semantics();\
    }

class GenSurfaceRegion: public Region {
public:
  GenSurfaceRegion( 
      const RegionNodeType node_type_ = RegionNodeType::Unset, 
      std::shared_ptr<Region> left_node_ = nullptr, 
      std::shared_ptr<Region> right_node_ = nullptr 
  )  : Region(node_type_,left_node_,right_node_)  {
  }
  void postprocess_in_ctor() override {}
  void check_semantics() const override;
  void set_initialized() override;

  bool __eq__(const GenSurfaceRegion& other) const;
  std::string to_str(const std::string ind="") const override;

  // --- attributes ---
  std::vector<int> element_connections;
  virtual void set_element_connections(const std::vector<int> new_element_connections_) {
    if (initialized) {
      throw RuntimeError("Value 'element_connections' of object with name " + name + " (class " + class_name + ")"
                         "cannot be set after model was initialized.");
    }
    element_connections = new_element_connections_;
  }
  virtual std::vector<int> get_element_connections() const {
    return element_connections;
  }

  std::shared_ptr<GeometryObject> parent;
  virtual void set_parent(std::shared_ptr<GeometryObject> new_parent_) {
    if (initialized) {
      throw RuntimeError("Value 'parent' of object with name " + name + " (class " + class_name + ")"
                         "cannot be set after model was initialized.");
    }
    parent = new_parent_;
  }
  virtual std::shared_ptr<GeometryObject> get_parent() const {
    return parent;
  }

  // --- methods ---
}; // GenSurfaceRegion

class SurfaceRegion;
py::class_<SurfaceRegion> define_pybinding_SurfaceRegion(py::module& m);
} // namespace API
} // namespace MCell

#endif // API_GEN_SURFACE_REGION_H

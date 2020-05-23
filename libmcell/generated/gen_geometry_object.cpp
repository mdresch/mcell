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

#include <sstream>
#include <pybind11/stl.h>
#include "gen_geometry_object.h"
#include "../api/geometry_object.h"
#include "../api/region.h"
#include "../api/surface_area.h"

namespace MCell {
namespace API {

void GenGeometryObject::check_semantics() const {
  if (!is_set(name)) {
    throw ValueError("Parameter 'name' must be set.");
  }
  if (!is_set(vertex_list)) {
    throw ValueError("Parameter 'vertex_list' must be set.");
  }
  if (!is_set(element_connections)) {
    throw ValueError("Parameter 'element_connections' must be set.");
  }
}

bool GenGeometryObject::__eq__(const GenGeometryObject& other) const {
  return
    name == other.name &&
    name == other.name &&
    vertex_list == other.vertex_list &&
    element_connections == other.element_connections &&
    vec_ptr_eq(surface_areas, other.surface_areas);
}

void GenGeometryObject::set_initialized() {
  vec_set_initialized(surface_areas);
  initialized = true;
}

std::string GenGeometryObject::to_str(const std::string ind) const {
  std::stringstream ss;
  ss << get_object_name() << ": " <<
      "name=" << name << ", " <<
      "vertex_list=" << vec_nonptr_to_str(vertex_list, ind + "  ") << ", " <<
      "element_connections=" << vec_nonptr_to_str(element_connections, ind + "  ") << ", " <<
      "\n" << ind + "  " << "surface_areas=" << vec_ptr_to_str(surface_areas, ind + "  ");
  return ss.str();
}

py::class_<GeometryObject> define_pybinding_GeometryObject(py::module& m) {
  return py::class_<GeometryObject, std::shared_ptr<GeometryObject>>(m, "GeometryObject")
      .def(
          py::init<
            const std::string&,
            const std::vector<std::vector<float_t>>,
            const std::vector<std::vector<int>>,
            const std::vector<std::shared_ptr<SurfaceArea>>
          >(),
          py::arg("name"),
          py::arg("vertex_list"),
          py::arg("element_connections"),
          py::arg("surface_areas") = std::vector<std::shared_ptr<SurfaceArea>>()
      )
      .def("check_semantics", &GeometryObject::check_semantics)
      .def("__str__", &GeometryObject::to_str, py::arg("ind") = std::string(""))
      .def("as_region", &GeometryObject::as_region)
      .def("dump", &GeometryObject::dump)
      .def_property("name", &GeometryObject::get_name, &GeometryObject::set_name)
      .def_property("vertex_list", &GeometryObject::get_vertex_list, &GeometryObject::set_vertex_list)
      .def_property("element_connections", &GeometryObject::get_element_connections, &GeometryObject::set_element_connections)
      .def_property("surface_areas", &GeometryObject::get_surface_areas, &GeometryObject::set_surface_areas)
    ;
}

} // namespace API
} // namespace MCell


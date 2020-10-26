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

#ifndef API_GEN_COMPLEX_H
#define API_GEN_COMPLEX_H

#include "../api/common.h"
#include "../api/base_data_class.h"

namespace MCell {
namespace API {

class ElementaryMoleculeInstance;
class Species;

#define COMPLEX_CTOR() \
    Complex( \
        const std::string& name_ = STR_UNSET, \
        const std::vector<std::shared_ptr<ElementaryMoleculeInstance>> elementary_molecule_instances_ = std::vector<std::shared_ptr<ElementaryMoleculeInstance>>(), \
        const Orientation orientation_ = Orientation::DEFAULT, \
        const std::string& compartment_name_ = STR_UNSET \
    ) { \
      class_name = "Complex"; \
      name = name_; \
      elementary_molecule_instances = elementary_molecule_instances_; \
      orientation = orientation_; \
      compartment_name = compartment_name_; \
      postprocess_in_ctor();\
      check_semantics();\
    }

class GenComplex: public BaseDataClass {
public:
  void postprocess_in_ctor() override {}
  void check_semantics() const override;
  void set_initialized() override;
  bool __eq__(const GenComplex& other) const;
  void set_all_attributes_as_default_or_unset() override;

  std::string to_str(const std::string ind="") const override;

  // --- attributes ---
  std::vector<std::shared_ptr<ElementaryMoleculeInstance>> elementary_molecule_instances;
  virtual void set_elementary_molecule_instances(const std::vector<std::shared_ptr<ElementaryMoleculeInstance>> new_elementary_molecule_instances_) {
    if (initialized) {
      throw RuntimeError("Value 'elementary_molecule_instances' of object with name " + name + " (class " + class_name + ") "
                         "cannot be set after model was initialized.");
    }
    elementary_molecule_instances = new_elementary_molecule_instances_;
  }
  virtual std::vector<std::shared_ptr<ElementaryMoleculeInstance>> get_elementary_molecule_instances() const {
    return elementary_molecule_instances;
  }

  Orientation orientation;
  virtual void set_orientation(const Orientation new_orientation_) {
    if (initialized) {
      throw RuntimeError("Value 'orientation' of object with name " + name + " (class " + class_name + ") "
                         "cannot be set after model was initialized.");
    }
    orientation = new_orientation_;
  }
  virtual Orientation get_orientation() const {
    return orientation;
  }

  std::string compartment_name;
  virtual void set_compartment_name(const std::string& new_compartment_name_) {
    if (initialized) {
      throw RuntimeError("Value 'compartment_name' of object with name " + name + " (class " + class_name + ") "
                         "cannot be set after model was initialized.");
    }
    compartment_name = new_compartment_name_;
  }
  virtual const std::string& get_compartment_name() const {
    return compartment_name;
  }

  // --- methods ---
  virtual std::string to_bngl_str() = 0;
  virtual std::shared_ptr<Species> as_species() = 0;
}; // GenComplex

class Complex;
py::class_<Complex> define_pybinding_Complex(py::module& m);
} // namespace API
} // namespace MCell

#endif // API_GEN_COMPLEX_H
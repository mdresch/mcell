/*
 * complex_species.cpp
 *
 *  Created on: Jan 9, 2020
 *      Author: ahusar
 */
#include <iostream>

#include "bng/ast.h"
#include "bng/bng_engine.h"
#include "bng/cplx_instance.h"
#include "bng/mol_type.h"

using namespace std;

namespace BNG {

// ------------- ComponentInstance -------------
void ComponentInstance::dump(const BNGData& bng_data, ostream& out) const {

  const ComponentType& ct = bng_data.get_component_type(component_type_id);
  out << ct.name;

  assert(state_id != STATE_ID_INVALID);
  if (state_id != STATE_ID_DONT_CARE) {
    out << "~" << bng_data.get_state_name(state_id);
  }

  assert(state_id != BOND_VALUE_INVALID);
  if (bond_value == BOND_VALUE_ANY) {
    out << "!" + BOND_STR_ANY;
  }
  else if (bond_value != BOND_VALUE_NO_BOND) {
    out << "!"  << bond_value;
  }
}


// ------------- MoleculeInstance -------------
void MolInstance::initialize_components_types(const MolType& mt) {
  for (component_type_id_t component_type_id: mt.component_type_ids) {
    // state is don't care, no bond
    component_instances.push_back(component_type_id);
  }
}


// searches for component with name
uint MolInstance::get_corresponding_component_index(
    const BNGData& bng_data,
    const MolType& mt,
    const std::string& name,
    const uint starting_index
) const {
  for (uint i = starting_index; i < mt.component_type_ids.size(); i++) {
    const ComponentType& ct = bng_data.get_component_type(mt.component_type_ids[i]);

    // we are looking for the first component with the same name
    if (ct.name == name) {
      return i;
    }
  }

  return INDEX_INVALID;
}


void MolInstance::dump(const BNGData& bng_data, const bool only_explicit, ostream& out) const {
  const MolType& mt = bng_data.get_molecule_type(mol_type_id);
  out << mt.name << "(";

  bool first_component = true;
  for (size_t i = 0; i < component_instances.size(); i++) {

    if (!only_explicit || component_instances[i].explicitly_listed_in_pattern) {
      if (!first_component) {
        out << ",";
      }

      component_instances[i].dump(bng_data, out);

      first_component = false;
    }
  }
  out << ")";
}


bool MolInstance::matches(const MolInstance& inst, const bool ignore_orientation) const {
  if (component_instances.size() == 0 && inst.component_instances.size() == 0) {

    if (ignore_orientation) {
      return mol_type_id == inst.mol_type_id;
    }
    else {
      return *this == inst;
    }
  }
  else {
    assert(false && "Support for BNG style matching is not implemented yet");
    return false;
  }
}

} /* namespace BNG */
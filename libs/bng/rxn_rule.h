/*
 * rule.h
 *
 *  Created on: Jan 9, 2020
 *      Author: ahusar
 */

#ifndef LIBS_BNG_RXN_RULE_H_
#define LIBS_BNG_RXN_RULE_H_

#include <string>
#include <iostream>

#include "bng/bng_defines.h"

#include "bng/cplx_instance.h"

namespace BNG {

class BNGData;
class SpeciesContainer;
class RxnClass;

struct CplxIndexPair {
  CplxIndexPair(const uint reactant_index_, const uint product_index_)
    : reactant_index(reactant_index_), product_index(product_index_) {
  }

  uint reactant_index;
  uint product_index;
};


struct RxnRateInfo {
  float_t time;
  float_t rate_constant;

  bool operator < (const RxnRateInfo& ri2) const {
    return time < ri2.time;
  }
};

enum class RxnType {
  Invalid,
  Standard, // any other reaction than below
  Transparent,
  Reflect,
  AbsorbRegionBorder
};


// BNG reaction rule
// rules are only unidirectional,
// if there is a reversible reaction in BNGL definition,
// two RxnRules are created
class RxnRule: public BaseFlag {
public:
  std::string name;
  rxn_rule_id_t id;

  RxnType type;

  // the complex species of reactants are patterns
  CplxInstanceVector reactants;
  CplxInstanceVector products;

  // base rate constant for this reaction as obtained from the BNGL or other input
  float_t base_rate_constant;

  // rate constant multiplier that must be used to
  // adjusted to the multiplicity of the reaction,
  // examples:
  // unimolecular reaction X(p~0) -> X(p~1) on molecule type X(p~0~1,p~0~1)
  // has multiplier of 2 because it can be applied to either one of the components
  // bimolecular reaction ?? TODO - example

  // on the other hand, bimolecular reaction X(a) + Y -> X(a!1) applied on complex X(a,b!1).X(a,b!1)
  // has multiplier of 1 because the reactant is not known until the actual reaction occurs
  //
  float_t rate_constant_multiplier;

  small_vector<RxnRateInfo> base_variable_rates;

  // set to true if it was possible to do a mapping between reactants and products
  bool mol_instances_are_fully_maintained;

  // caching
  uint_set<species_id_t> species_applicable_as_reactants;
  uint_set<species_id_t> species_not_applicable_as_reactants;

private:
  uint num_surf_products;

  // variable reaction rate constants, sorted by time
  // index is initialized to 0
  uint next_variable_rate_index;

  // maintain information on where this reaction was used in order to
  // update all classes if this reaction's rate constant changes
  std::set<RxnClass*> rxn_classes_where_used;

public:
  RxnRule(const BNGData* bng_data_)
    : id(RXN_RULE_ID_INVALID), type(RxnType::Invalid), mol_instances_are_fully_maintained(false),
      base_rate_constant(FLT_INVALID), rate_constant_multiplier(1),
      num_surf_products(UINT_INVALID), next_variable_rate_index(0),
      bng_data(bng_data_)
      {
  }

  void finalize();

  // BNGL style reaction handling is implemented in this method
  void create_products_for_complex_rxn(
      const std::vector<const CplxInstance*>& input_reactants,
      std::vector<CplxInstance>& created_products
  ) const;

  float_t get_rate_constant() const {
    return base_rate_constant * rate_constant_multiplier;
  }

  const CplxInstance& get_cplx_reactant(const uint index) const {
    assert(index <= reactants.size());
    return reactants[index];
  }

  const CplxInstance& get_cplx_product(const uint index) const {
    assert(index <= products.size());
    return products[index];
  }

  // mcell3 variant of maintaining substances,
  // e.g. for A + B -> A : reactant A is maintained
  bool is_cplx_reactant_on_both_sides_of_rxn(const uint index) const;
  bool is_cplx_product_on_both_sides_of_rxn(const uint index) const;

  bool operator ==(const RxnRule& rr2) {
    // ordering of components in a molecule is not important
    // two component types must have the same id, this is ensured in find_or_add_component_type
    return
        name == rr2.name &&
        reactants == rr2.reactants && products == rr2.products &&
        base_rate_constant == rr2.base_rate_constant;
  }

  // used in semantic check
  bool check_reactants_products_mapping(std::ostream& out);


  void append_reactant(const CplxInstance& inst) {
    reactants.push_back(inst);
  }

  void append_product(const CplxInstance& inst) {
    products.push_back(inst);
  }

  uint get_num_surf_products() const { // we don't have probably the information that is needed
    assert(is_finalized());
    return num_surf_products;
  }

  uint get_num_players() const {
    return reactants.size() + products.size();
  }

  bool is_unimol() const {
    return reactants.size() == 1;
  }

  bool is_bimol() const {
    return reactants.size() == 2;
  }

  bool is_absorptive_region_rxn() const {
    return is_bimol() && reactants[1].is_reactive_surface() && products.empty();
  }

  bool is_bimol_vol_rxn() const {
    if (is_unimol()) {
      return false;
    }
    else if (is_bimol()) {
      return reactants[0].is_vol() && reactants[1].is_vol();
    }
    else {
      assert(false);
      return false;
    }
  }

  bool is_surf_rxn() const {
    if (is_unimol()) {
      return reactants[0].is_surf();
    }
    else if (is_bimol()) {
      return reactants[0].is_surf() || reactants[1].is_surf();
    }
    else {
      assert(false);
      return false;
    }
  }

  bool is_reactive_surface_rxn() const {
    if (is_unimol()) {
      return false;
    }
    else if (is_bimol()) {
      return reactants[0].is_reactive_surface() || reactants[1].is_reactive_surface();
    }
    else {
      assert(false);
      return false;
    }
  }

  // returns true if species 'id' matches one of the reactants
  bool species_can_be_reactant(const species_id_t id, const SpeciesContainer& all_species);

  // returns true if both species can be used as separate reactants for a bimol rxn
  bool species_can_be_bimol_reactants(
      const species_id_t id1, const species_id_t id2, const SpeciesContainer& all_species
  );

  // returns true if two reactants match each other and species 'id' matches one of the reactants
  bool species_is_both_bimol_reactants(const species_id_t id, const SpeciesContainer& all_species);

  bool get_assigned_simple_cplx_reactant_for_product(const uint product_index, uint& reactant_index) const;

  void set_is_counted() {
    set_flag(RXN_FLAG_COUNTED);
  }

  bool is_counted() const {
    return has_flag(RXN_FLAG_COUNTED);
  }

  bool is_simple() const {
    return has_flag(RXN_FLAG_SIMPLE);
  }

  void add_rxn_class_where_used(RxnClass* rxn_class) {
    rxn_classes_where_used.insert(rxn_class);
  }

  // returns false when there are no variable rates or we already processed all scheduled times
  bool may_update_rxn_rate() const {
    return next_variable_rate_index < (int)base_variable_rates.size();
  }

  // returns true if rate was updated
  // requester is the rxn class that requested this update
  bool update_variable_rxn_rate(const float_t current_time, const RxnClass* requester);

  float_t get_next_time_of_rxn_rate_update() const {
    if (may_update_rxn_rate()) {
      return base_variable_rates[next_variable_rate_index].time;
    }
    else {
      return TIME_FOREVER;
    }
  }

  std::string to_str(const bool with_rate_constant = true) const;
  std::string reactants_to_str() const;
  std::string products_to_str() const;
  void dump(const bool for_diff = false, const std::string ind = "") const;

private:
  void create_patterns_graph();
  void create_products_graph();

  void move_products_that_are_also_reactants_to_be_the_first_products();

  // checks if it is possible to create a mapping from reactants to products and
  // sets members molecule_instances_are_maintained and mapping
  void compute_reactants_products_mapping();

  void compute_rate_constant_multiplier();

  bool check_components_mapping(
      const MolInstance& first_mi,
      const MolInstance& second_mi,
      const char* msg,
      std::ostream& out
  );

  bool check_components_states(
      const MolInstance& prod_mi,
      const MolInstance& pat_mi,
      std::ostream& out
  );

  std::string complex_instance_vector_to_str(const CplxInstanceVector& complexes) const;
  void dump_complex_instance_vector(
      const CplxInstanceVector& complexes,
      const std::string ind) const;


  // mutable is needed because of usage in create_products_for_complex_rxn
  // the graphs are not modified, but boost cannot use them as const
  mutable Graph patterns_graph; // graphs based on reactants
  mutable Graph products_graph;
  VertexMapping products_to_patterns_mapping;

  // information for MCell3 reactions,
  // maps simple complexes from their pattern to the product
  // ignores complex complexes
  small_vector<CplxIndexPair> simple_cplx_mapping;

  const BNGData* bng_data; // needed to create results of complex reactions
};

} /* namespace BNG */

#endif /* LIBS_BNG_RXN_RULE_H_ */

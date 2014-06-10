/***********************************************************************************
 *                                                                                 *
 * Copyright (C) 2006-2014 by *
 * The Salk Institute for Biological Studies and *
 * Pittsburgh Supercomputing Center, Carnegie Mellon University *
 *                                                                                 *
 * This program is free software; you can redistribute it and/or *
 * modify it under the terms of the GNU General Public License *
 * as published by the Free Software Foundation; either version 2 *
 * of the License, or (at your option) any later version. *
 *                                                                                 *
 * This program is distributed in the hope that it will be useful, *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the *
 * GNU General Public License for more details. *
 *                                                                                 *
 * You should have received a copy of the GNU General Public License *
 * along with this program; if not, write to the Free Software *
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
 *USA. *
 *                                                                                 *
 ***********************************************************************************/

#if defined(__linux__)
#define _GNU_SOURCE 1
#endif

#ifndef _WIN32
#include <sys/resource.h>
#endif
#include <stdlib.h>
#if defined(__linux__)
#include <fenv.h>
#endif

#include <assert.h>
#include <signal.h>
#include <string.h>
#include <time.h>

#include "argparse.h"
#include "chkpt.h"
#include "count_util.h"
#include "diffuse_util.h"
#include "init.h"
#include "libmcell.h"
#include "logging.h"
#include "react_output.h"
#include "react_util.h"
#include "sym_table.h"
#include "version_info.h"
#include "create_species.h"
#include "create_reactions.h"
#include "create_reaction_output.h"
#include "create_viz_output.h"
#include "create_object.h"
#include "create_geometry.h"
#include "create_release_site.h"

/* simple wrapper for executing the supplied function call. In case
 * of an error returns with MCELL_FAIL and prints out error_message */
#define CHECKED_CALL(function, error_message)                                  \
  {                                                                            \
    if (function) {                                                            \
      mcell_log(error_message);                                                \
      return MCELL_FAIL;                                                       \
    }                                                                          \
  }

/* declaration of static functions */
static int install_usr_signal_handlers(void);
void swap_double(double *x, double *y);

struct output_column *get_counter_trigger_column(MCELL_STATE *state,
                                                 const char *counter_name,
                                                 int column_id);

/************************************************************************
 *
 * function for initializing the main mcell simulator. MCELL_STATE
 * keeps track of the state of the simulation.
 *
 * Returns NULL on error and a pointer to MCELL_STATE otherwise
 *
 ************************************************************************/
MCELL_STATE *
mcell_create() {
  // signal handlers
  if (install_usr_signal_handlers()) {
    return NULL;
  }

  // logging
  mcell_set_log_file(stdout);
  mcell_set_error_file(stderr);

  MCELL_STATE *state = CHECKED_MALLOC_STRUCT_NODIE(struct volume, "world");
  if (state == NULL) {
    return NULL;
  }
  memset(state, 0, sizeof(struct volume));

#if defined(__linux__)
  feenableexcept(FE_DIVBYZERO);
#endif

  state->procnum = 0;
  state->rx_hashsize = 0;
  state->iterations = INT_MIN; /* indicates iterations not set */
  state->chkpt_infile = NULL;
  state->chkpt_outfile = NULL;
  state->chkpt_init = 1;
  state->log_freq =
      ULONG_MAX; /* Indicates that this value has not been set by user */
  state->seed_seq = 1;
  state->with_checks_flag = 1;

  time_t begin_time_of_day;
  time(&begin_time_of_day);
  state->begin_timestamp = begin_time_of_day;
  state->initialization_state = "initializing";

  if (!(state->var_sym_table = init_symtab(1024))) {
    mcell_log("Failed to initialize MDL variable symbol table.");
    return NULL;
  }

  return state;
}

/************************************************************************
 *
 * function for initializing the intial simulation state (variables,
 * notifications, data structures)
 *
 * Returns 1 on error and 0 on success
 *
 ************************************************************************/
MCELL_STATUS
mcell_init_state(MCELL_STATE *state) {
  CHECKED_CALL(
      init_notifications(state),
      "Unknown error while initializing user-notification data structures.");

  CHECKED_CALL(init_variables(state),
               "Unknown error while initializing system variables.");

  CHECKED_CALL(init_data_structures(state),
               "Unknown error while initializing system data structures.");

  return MCELL_SUCCESS;
}

/************************************************************************
 *
 * function for parsing the models underlying mdl file. The function
 * updates the state accordingly.
 *
 * Returns 0 on sucess and 1 on error
 *
 * NOTE: This is currently just a very thin wrapper around parse_input()
 *
 ************************************************************************/
MCELL_STATUS
mcell_parse_mdl(MCELL_STATE *state) { return parse_input(state); }

/************************************************************************
 *
 * function for setting up all the internal data structure to get the
 * simulation into a runnable state.
 *
 * NOTE: Before this function can be called the engine user code
 *       either needs to call
 *       - mcell_parse_mdl() to parse a valid MDL file or
 *       - the individiual API functions for adding model elements
 *         (molecules, geometry, ...)
 *         XXX: These functions don't exist yet!
 *
 * Returns 0 on sucess and 1 on error
 *
 * NOTE: This is currently just a very thin wrapper around parse_input()
 *
 ************************************************************************/
MCELL_STATUS
mcell_init_simulation(MCELL_STATE *state) {
  CHECKED_CALL(init_reactions(state), "Error initializing reactions.");

  CHECKED_CALL(init_species(state), "Error initializing species.");

  if (state->notify->progress_report != NOTIFY_NONE)
    mcell_log("Creating geometry (this may take some time)");

  CHECKED_CALL(init_geom(state), "Error initializing geometry.");
  CHECKED_CALL(init_partitions(state), "Error initializing partitions.");
  CHECKED_CALL(init_vertices_walls(state),
               "Error initializing vertices and walls.");
  CHECKED_CALL(init_regions(state), "Error initializing regions.");

  if (state->place_waypoints_flag) {
    CHECKED_CALL(place_waypoints(state), "Error while placing waypoints.");
  }

  if (state->with_checks_flag) {
    CHECKED_CALL(check_for_overlapped_walls(state->n_subvols, state->subvol),
                 "Error while checking for overlapped walls.");
  }

  CHECKED_CALL(init_effectors(state),
               "Error while placing effectors on regions.");
  CHECKED_CALL(init_releases(state), "Error while initializing release sites.");
  CHECKED_CALL(init_counter_name_hash(state),
               "Error while initializing counter name hash.");

  return MCELL_SUCCESS;
}

/************************************************************************
 *
 * function for reading and initializing the checkpoint if requested
 *
 * Returns 1 on error and 0 on success
 *
 ************************************************************************/
MCELL_STATUS
mcell_init_read_checkpoint(MCELL_STATE *state) {

  if (state->chkpt_flag == 1) {
    long long exec_iterations;
    CHECKED_CALL(init_checkpoint_state(state, &exec_iterations),
                 "Error while initializing checkpoint.");

    /* XXX This is a hack to be backward compatible with the previous
     * MCell behaviour. Basically, as soon as exec_iterations <= 0
     * MCell will stop and we emulate this by returning 1 even though
     * this is not an error (as implied by returning 1). */
    if (exec_iterations <= 0) {
      mem_dump_stats(mcell_get_log_file());
      return MCELL_FAIL;
    }
  } else {
    state->chkpt_seq_num = 1;
  }

  if (state->chkpt_infile) {
    CHECKED_CALL(load_checkpoint(state),
                 "Error while loading previous checkpoint.");
  }

  // set the iteration time to the start time of the checkpoint
  state->it_time = state->start_time;

  return MCELL_SUCCESS;
}

/************************************************************************
 *
 * function for initializing the viz and reaction data output
 *
 * XXX: This function has to be called last, i.e. after the
 *      simulation has been initialized and checkpoint information
 *      been read.
 *
 * Returns 1 on error and 0 on success
 *
 ************************************************************************/
MCELL_STATUS
mcell_init_output(MCELL_STATE *state) {
  CHECKED_CALL(init_viz_data(state), "Error while initializing viz data.");
  CHECKED_CALL(init_reaction_data(state),
               "Error while initializing reaction data.");
  CHECKED_CALL(init_timers(state), "Error initializing the simulation timers.");

  // signal successful end of simulation
  state->initialization_state = NULL;

  return MCELL_SUCCESS;
}

/************************************************************************
 *
 * function for retrieving the current value of a given count
 * expression
 *
 * The call expects:
 *
 * - MCELL_STATE
 * - counter_name: a string containing the name of the count statement to
 *   be retrieved. Currently, the name is identical to the full path to which
 *   the corresponding reaction output will be written but this may change
 *   in the future
 * - column: int describing the column to be retrieved
 * - count_data: a *double which will receive the actual value
 * - count_data_type: a *count_type_t which will receive the type of the
 *   data (for casting of count_data)
 *
 * NOTE: This function can be called anytime after the
 *       REACTION_DATA_OUTPUT has been either parsed or
 *       set up with API calls.
 *
 * Returns 1 on error and 0 on success
 *
 ************************************************************************/
MCELL_STATUS
mcell_get_counter_value(MCELL_STATE *state, const char *counter_name,
                        int column_id, double *count_data,
                        enum count_type_t *count_data_type) {
  struct output_column *column = NULL;
  if ((column = get_counter_trigger_column(state, counter_name, column_id)) ==
      NULL) {
    return MCELL_FAIL;
  }

  // if we happen to encounter trigger data we bail
  if (column->data_type == COUNT_TRIG_STRUCT) {
    return MCELL_FAIL;
  }

  // evaluate the expression and retrieve it
  eval_oexpr_tree(column->expr, 1);
  *count_data = (double)column->expr->value;
  *count_data_type = column->data_type;

  return MCELL_SUCCESS;
}

/************************************************************************
 *
 * function for changing the reaction rate constant of a given named
 * reaction.
 *
 * The call expects:
 *
 * - MCELL_STATE
 * - reaction name: const char* containing the name of reaction
 * - new rate: a double with the new reaction rate constant
 *
 * NOTE: This function can be called anytime after the
 *       REACTION_DATA_OUTPUT has been either parsed or
 *       set up with API calls.
 *
 * Returns 1 on error and 0 on success
 *
 ************************************************************************/
MCELL_STATUS
mcell_change_reaction_rate(MCELL_STATE *state, const char *reaction_name,
                           double new_rate) {
  // sanity check
  if (new_rate < 0.0) {
    return MCELL_FAIL;
  }

  // retrive reaction corresponding to name if it exists
  struct rxn *rx = NULL;
  int path_id = 0;
  if (get_rxn_by_name(state->reaction_hash, state->rx_hashsize, reaction_name,
                      &rx, &path_id)) {
    return MCELL_FAIL;
  }

  // now change the rate
  if (change_reaction_probability(state, rx, path_id, new_rate)) {
    return MCELL_FAIL;
  }

  return MCELL_SUCCESS;
}

/*************************************************************************
 *
 * mcell_add_reaction add a single reaction described by reaction_def to
 * the simulations.
 *
 *************************************************************************/
MCELL_STATUS
mcell_add_reaction(MCELL_STATE *state, struct mcell_species *reactants,
                   struct reaction_arrow *react_arrow,
                   struct mcell_species *surf_class,
                   struct mcell_species *products, struct sym_table *pathname,
                   struct reaction_rates *rates, const char *rate_filename) {
  char *rx_name;
  struct sym_table *symp;
  int bidirectional = 0;
  int num_surf_products = 0;
  struct rxn *rxnp;

  /* Create pathway */
  struct pathway *pathp = (struct pathway *)CHECKED_MALLOC_STRUCT(
      struct pathway, "reaction pathway");
  if (pathp == NULL) {
    return MCELL_FAIL;
  }
  memset(pathp, 0, sizeof(struct pathway));

  /* Scan reactants, copying into the new pathway */
  int num_vol_mols = 0;
  int num_grid_mols = 0;
  int all_3d = 1;
  int complex_type = 0;
  int reactant_idx = 0;
  int oriented_count = 0;
  int num_complex_reactants = 0;
  if (extract_reactants(pathp, reactants, &reactant_idx, &num_vol_mols,
                        &num_grid_mols, &num_complex_reactants, &all_3d,
                        &oriented_count, &complex_type) == MCELL_FAIL) {
    return MCELL_FAIL;
  }

  /* Only one complex reactant allowed */
  if (num_complex_reactants > 1) {
    mcell_error("Reaction may not include more than one reactant which is a "
                "subunit in a complex.");
    return MCELL_FAIL;
  }

  /* Grab info from the arrow */
  if (react_arrow->flags & ARROW_BIDIRECTIONAL) {
    bidirectional = 1;
  }

  int catalytic = -1;
  if (react_arrow->flags & ARROW_CATALYTIC) {
    if (extract_catalytic_arrow(pathp, react_arrow, &reactant_idx,
                                &num_vol_mols, &num_grid_mols, &all_3d,
                                &oriented_count) == MCELL_FAIL) {
      return MCELL_FAIL;
    }
    catalytic = reactant_idx - 1;
  }

  /* If a surface was specified, include it */
  int surface = -1;
  unsigned int num_surfaces = 0;
  if (surf_class->mol_type != NULL) {
    if (extract_surface(pathp, surf_class, &reactant_idx, &num_surfaces,
                        &oriented_count) == MCELL_FAIL) {
      return MCELL_FAIL;
    }
    surface = reactant_idx - 1;
    all_3d = 0;
  }

  /* Create a reaction name for the pathway we're creating */
  rx_name = create_rx_name(pathp);
  if (rx_name == NULL) {
    mcell_error("Out of memory while creating reaction.");
    return MCELL_FAIL;
  }

  /* If this reaction doesn't exist, create it */
  if ((symp = retrieve_sym(rx_name, state->rxn_sym_table)) != NULL) {
    /* do nothing */
  } else if ((symp = store_sym(rx_name, RX, state->rxn_sym_table, NULL)) ==
             NULL) {
    mcell_error("Out of memory while creating reaction.");
    free(rx_name);
    return MCELL_FAIL;
  }
  free(rx_name);

  rxnp = (struct rxn *)symp->value;
  rxnp->n_reactants = reactant_idx;
  ++rxnp->n_pathways;

  /* Check for invalid reaction specifications */
  if (check_surface_specs(state, rxnp->n_reactants, num_surfaces, num_vol_mols,
                          all_3d, oriented_count) == MCELL_FAIL) {
    return MCELL_FAIL;
  }

  /* Add catalytic reagents to the product list.
   *    - For unidirectional catalytic reactions - copy catalyst to products
   *      only if catalyst is not a surface_clas.
   *    - For bidirectional catalytic reactions always copy catalyst to
   *      products and take care that surface_class will not appear in the
   *      products later after inverting the reaction
   */
  if (catalytic >= 0) {
    if (add_catalytic_species_to_products(pathp, catalytic, bidirectional,
                                          all_3d) == MCELL_FAIL) {
      return MCELL_FAIL;
    }
  }

  /* Add in all products */
  int num_complex_products = 0;
  if (extract_products(state, pathp, products, &num_surf_products,
                       &num_complex_products, bidirectional, complex_type,
                       all_3d) == MCELL_FAIL) {
    return MCELL_FAIL;
  }
  // mem_put_list(parse_state->mol_data_list_mem, products);

  /* Subunits can neither be created nor destroyed */
  if (num_complex_reactants != num_complex_products) {
    mcell_error_raw(
        "Reaction must include the same number of complex-subunits "
        "on each side of the reaction (have %d reactants vs. %d products)",
        num_complex_reactants, num_complex_products);
    return MCELL_FAIL;
  }

  /* Attach reaction pathway name, if we have one */
  if (pathname != NULL) {
    struct rxn_pathname *rxpnp = (struct rxn_pathname *)pathname->value;
    rxpnp->rx = rxnp;
    pathp->pathname = rxpnp;
  }

  if (pathp->product_head != NULL) {
    pathp->prod_signature = create_prod_signature(&pathp->product_head);
    if (pathp->prod_signature == NULL) {
      mcell_error(
          "Error creating 'prod_signature' field for the reaction pathway.");
      return MCELL_FAIL;
    }
  } else
    pathp->prod_signature = NULL;

  /* Copy in forward rate */
  switch (rates->forward_rate.rate_type) {
  case RATE_UNSET:
    mcell_error_raw("File %s, Line %d: Internal error: Rate is not set",
                    __FILE__, __LINE__);
    return MCELL_FAIL;

  case RATE_CONSTANT:
    pathp->km = rates->forward_rate.v.rate_constant;
    pathp->km_filename = NULL;
    pathp->km_complex = NULL;
    break;

  case RATE_FILE:
    pathp->km = 0.0;
    pathp->km_filename = (char *)rate_filename;
    free(rates->forward_rate.v.rate_file);
    pathp->km_complex = NULL;
    break;

  case RATE_COMPLEX:
    pathp->km = 0.0;
    pathp->km_filename = NULL;
    pathp->km_complex = rates->forward_rate.v.rate_complex;
    break;

  default:
    UNHANDLED_CASE(rates->forward_rate.rate_type);
  }

  /* Add the pathway to the list for this reaction */
  if (rates->forward_rate.rate_type == RATE_FILE) {
    struct pathway *tpp;
    if (rxnp->pathway_head == NULL) {
      rxnp->pathway_head = pathp;
      pathp->next = NULL;
    } else /* Move varying reactions to the end of the list */
    {
      for (tpp = rxnp->pathway_head;
           tpp->next != NULL && tpp->next->km_filename == NULL;
           tpp = tpp->next) {
      }
      pathp->next = tpp->next;
      tpp->next = pathp;
    }
  } else {
    pathp->next = rxnp->pathway_head;
    rxnp->pathway_head = pathp;
  }

  /* If we're doing 3D releases, set up array so we can release reversibly */
  if (state->r_step_release == NULL && all_3d && pathp->product_head != NULL) {
    state->r_step_release = init_r_step_3d_release(state->radial_subdivisions);
    if (state->r_step_release == NULL) {
      mcell_error("Out of memory building r_step array.");
      return MCELL_FAIL;
    }
  }

  /* If the vacancy search distance is zero and this reaction produces more
   * grid molecules than it comsumes, it can never succeed, except if it is a
   * volume molecule hitting the surface and producing a single grid molecule.
   * Fail with an error message.
   */
  if ((state->vacancy_search_dist2 == 0) &&
      (num_surf_products > num_grid_mols)) {
    /* The case with one volume molecule reacting with the surface and
     * producing one grid molecule is okay.
     */
    if (num_grid_mols == 0 && num_vol_mols == 1 && num_surf_products == 1) {
      /* do nothing */
    } else {
      mcell_error("Error: number of surface products exceeds number of surface "
                  "reactants, but VACANCY_SEARCH_DISTANCE is not specified or "
                  "set to zero.");
      return MCELL_FAIL;
    }
  }

  /* A non-reversible reaction may not specify a reverse reaction rate */
  if (rates->backward_rate.rate_type != RATE_UNSET && !bidirectional) {
    mcell_error("Reverse rate specified but the reaction isn't reversible.");
    return MCELL_FAIL;
  }

  /* Create reverse reaction if we need to */
  if (bidirectional) {
    /* A bidirectional reaction must specify a reverse rate */
    if (rates->backward_rate.rate_type == RATE_UNSET) {
      // mdlerror(parse_state, "Reversible reaction indicated but no reverse
      // rate supplied.");
      return MCELL_FAIL;
    }

    /* if "surface_class" is present on the reactant side of the reaction copy
     * it to the product side of the reaction.
     *
     * Reversible reaction of the type:
     *    A' @ surf' <---> C''[>r1,<r2]
     *
     * is equivalent now to the two reactions:
     *    A' @ surf' ---> C'' [r1]
     *    C'' @ surf' ----> A' [r2]
     *
     * Reversible reaction of the type:
     *    A' + B' @ surf' <---> C'' + D'' [>r1,<r2]
     *
     * is equivalent now to the two reactions:
     *    A' + B @ surf' ---> C'' + D'' [r1]
     *    C'' + D'' @ surf' ----> A' + B' [r2]
     */
    if (surface != -1 && surface != catalytic) {
      struct product *prodp;
      prodp = (struct product *)CHECKED_MALLOC_STRUCT(struct product,
                                                      "reaction product");
      if (prodp == NULL) {
        // mem_put(parse_state->prod_mem, prodp);
        return MCELL_FAIL;
      }

      switch (surface) {
      case 1:
        prodp->prod = pathp->reactant2;
        prodp->orientation = pathp->orientation2;
        break;

      case 2:
        prodp->prod = pathp->reactant3;
        prodp->orientation = pathp->orientation3;
        break;

      case 0:
      default:
        mcell_internal_error(
            "Surface appears in invalid reactant slot in reaction (%d).",
            surface);
        break;
      }
      prodp->next = pathp->product_head;
      pathp->product_head = prodp;
    }

    /* Invert the current reaction pathway */
    if (invert_current_reaction_pathway(state, pathp, &rates->backward_rate,
                                        rate_filename)) {
      return MCELL_FAIL;
    }
  }

  return MCELL_SUCCESS;
}

/*************************************************************************
 concat_rx_name:
    Concatenates reactants onto a reaction name.  Reactants which are subunits
    in macromolecular complexes will have their names parenthesized.

 In:  parse_state: parser state
      name1: name of first reactant (or first part of reaction name)
      is_complex1: 0 unless the first reactant is a subunit in a complex
      name2: name of second reactant (or second part of reaction name)
      is_complex2: 0 unless the second reactant is a subunit in a complex
 Out: reaction name as a string, or NULL if an error occurred
*************************************************************************/
static char *concat_rx_name(char *name1, int is_complex1, char *name2,
                            int is_complex2) {
  char *rx_name;

  /* Make sure they aren't both subunits  */
  if (is_complex1 && is_complex2) {
    // mdlerror_fmt(parse_state, "File '%s', Line %ld: Internal error -- a
    // reaction cannot have two reactants which are subunits of a
    // macromolecule.", __FILE__, (long)__LINE__);
    return NULL;
  }

  /* Sort them */
  if (is_complex2 || strcmp(name2, name1) <= 0) {
    char *nametmp = name1;
    int is_complextmp = is_complex1;
    name1 = name2;
    is_complex1 = is_complex2;
    name2 = nametmp;
    is_complex2 = is_complextmp;
    assert(is_complex2 == 0);
  }

  /* Build the name */
  if (is_complex1)
    rx_name = CHECKED_SPRINTF("(%s)+%s", name1, name2);
  else
    rx_name = CHECKED_SPRINTF("%s+%s", name1, name2);

  /* Die if we failed to allocate memory */
  if (rx_name == NULL)
    return NULL;

  return rx_name;
}

/*************************************************************************
 *
 * mcell_add_surface_reaction adds a single surface reaction described
 * by reaction_def to the simulations.
 *
 *************************************************************************/
MCELL_STATUS
mcell_add_surface_reaction(MCELL_STATE *state, int reaction_type,
                           struct species *surface_class,
                           struct sym_table *reactant_sym, short orient) {
  struct species *reactant = (struct species *)reactant_sym->value;
  struct product *prodp;
  struct rxn *rxnp;
  // struct pathway *pathp;
  struct name_orient *no;

  /* Make sure the other reactant isn't a surface */
  if (reactant->flags == IS_SURFACE) {
    // mdlerror_fmt(parse_state,
    //             "Illegal reaction between two surfaces in surface reaction:
    // %s -%s-> ...",
    //             reactant_sym->name,
    //             surface_class->sym->name);
    return MCELL_FAIL;
  }

  /* Build reaction name */
  char *rx_name =
      concat_rx_name(surface_class->sym->name, 0, reactant_sym->name, 0);
  if (rx_name == NULL) {
    // mdlerror_fmt(parse_state,
    //             "Out of memory while parsing surface reaction: %s -%s-> ...",
    //             surface_class->sym->name,
    //             reactant_sym->name);
    return MCELL_FAIL;
  }

  /* Find or create reaction */
  struct sym_table *reaction_sym;
  if ((reaction_sym = retrieve_sym(rx_name, state->rxn_sym_table)) != NULL) {
    /* do nothing */
  } else if ((reaction_sym =
                  store_sym(rx_name, RX, state->rxn_sym_table, NULL)) == NULL) {
    free(rx_name);
    // mdlerror_fmt(parse_state,
    //             "Out of memory while creating surface reaction: %s -%s->
    // ...",
    //             reactant_sym->name,
    //             surface_class->sym->name);
    return MCELL_FAIL;
  }
  free(rx_name);

  /* Create pathway */
  struct pathway *pathp = (struct pathway *)CHECKED_MALLOC_STRUCT(
      struct pathway, "reaction pathway");

  if (pathp == NULL)
    return MCELL_FAIL;
  memset(pathp, 0, sizeof(struct pathway));

  rxnp = (struct rxn *)reaction_sym->value;
  rxnp->n_reactants = 2;
  ++rxnp->n_pathways;
  pathp->pathname = NULL;
  pathp->reactant1 = surface_class;
  pathp->reactant2 = (struct species *)reactant_sym->value;
  pathp->reactant3 = NULL;
  pathp->is_complex[0] = pathp->is_complex[1] = pathp->is_complex[2] = 0;
  pathp->km = GIGANTIC;
  pathp->km_filename = NULL;
  pathp->km_complex = NULL;
  pathp->prod_signature = NULL;
  pathp->flags = 0;

  pathp->orientation1 = 1;
  pathp->orientation3 = 0;
  if (orient == 0) {
    pathp->orientation2 = 0;
  } else {
    pathp->orientation2 = (orient < 0) ? -1 : 1;
  }

  no = CHECKED_MALLOC_STRUCT(struct name_orient, "struct name_orient");
  no->name = CHECKED_STRDUP(reactant->sym->name, "reactant name");
  if (orient == 0) {
    no->orient = 0;
  } else {
    no->orient = (orient < 0) ? -1 : 1;
  }

  switch (reaction_type) {
  case RFLCT:
    prodp = (struct product *)CHECKED_MALLOC_STRUCT(struct product,
                                                    "reaction product");
    if (prodp == NULL)
      return MCELL_FAIL;

    pathp->flags |= PATHW_REFLEC;
    prodp->prod = pathp->reactant2;
    prodp->orientation = 1;
    prodp->next = NULL;
    pathp->product_head = prodp;
    if (pathp->product_head != NULL) {
      pathp->prod_signature = create_prod_signature(&pathp->product_head);
      if (pathp->prod_signature == NULL) {
        // mdlerror(parse_state, "Error creating 'prod_signature' field for the
        // reaction pathway.");
        return MCELL_FAIL;
      }
    }
    if (surface_class->refl_mols == NULL) {
      no->next = NULL;
      surface_class->refl_mols = no;
    } else {
      no->next = surface_class->refl_mols;
      surface_class->refl_mols = no;
    }

    break;
  case TRANSP:
    prodp = (struct product *)CHECKED_MALLOC_STRUCT(struct product,
                                                    "reaction product");
    if (prodp == NULL)
      return MCELL_FAIL;

    pathp->flags |= PATHW_TRANSP;
    prodp->prod = pathp->reactant2;
    prodp->orientation = -1;
    prodp->next = NULL;
    pathp->product_head = prodp;
    if (pathp->product_head != NULL) {
      pathp->prod_signature = create_prod_signature(&pathp->product_head);
      if (pathp->prod_signature == NULL) {
        // mdlerror(parse_state, "Error creating 'prod_signature' field for the
        // reaction pathway.");
        return MCELL_FAIL;
      }
    }
    if (surface_class->transp_mols == NULL) {
      no->next = NULL;
      surface_class->transp_mols = no;
    } else {
      no->next = surface_class->transp_mols;
      surface_class->transp_mols = no;
    }
    break;
  case SINK:
    pathp->flags |= PATHW_ABSORP;
    pathp->product_head = NULL;
    if (surface_class->absorb_mols == NULL) {
      no->next = NULL;
      surface_class->absorb_mols = no;
    } else {
      no->next = surface_class->absorb_mols;
      surface_class->absorb_mols = no;
    }
    break;
  default:
    // mdlerror(parse_state, "Unknown special surface type.");
    return MCELL_FAIL;
    break;
  }

  pathp->next = rxnp->pathway_head;
  rxnp->pathway_head = pathp;

  return MCELL_SUCCESS;
}

/*************************************************************************
 *
 * mcell_add_surface_reaction adds a single surface reaction described
 * by reaction_def to the simulations.
 *
 *************************************************************************/
MCELL_STATUS
mcell_add_concentration_clamp(MCELL_STATE *state, struct species *surface_class,
                              struct sym_table *mol_sym, short orient,
                              double conc) {
  struct rxn *rxnp;
  struct pathway *pathp;
  struct sym_table *stp3;
  struct species *specp = (struct species *)mol_sym->value;
  struct name_orient *no;

  if (specp->flags == IS_SURFACE) {
    //    mdlerror_fmt(parse_state,
    //                "Illegal reaction between two surfaces in surface
    // reaction: %s -%s-> ...",
    //               mol_sym->name, surface_class->sym->name);
    return MCELL_FAIL;
  }
  if (specp->flags & ON_GRID) {
    // mdlerror(parse_state, "Concentration clamp does not work on surface
    // molecules.");
    return MCELL_FAIL;
  }
  if (specp->flags & NOT_FREE || specp->D <= 0.0) {
    //    mdlerror(parse_state, "Concentration clamp must be applied to molecule
    // diffusing in 3D");
    return MCELL_FAIL;
  }
  if (conc < 0) {
    // mdlerror(parse_state, "Concentration can only be clamped to positive
    // values.");
    return MCELL_FAIL;
  }

  char *rx_name = concat_rx_name(surface_class->sym->name, 0, mol_sym->name, 0);
  if (rx_name == NULL) {
    //    mdlerror_fmt(parse_state,
    //                 "Memory allocation error: %s -%s-> ...",
    //                 surface_class->sym->name, mol_sym->name);
    return MCELL_FAIL;
  }
  if ((stp3 = retrieve_sym(rx_name, state->rxn_sym_table)) != NULL) {
    /* do nothing */
  } else if ((stp3 = store_sym(rx_name, RX, state->rxn_sym_table, NULL)) ==
             NULL) {
    free(rx_name);
    //    mdlerror_fmt(parse_state,
    //                 "Cannot store surface reaction: %s -%s-> ...",
    //                 mol_sym->name, surface_class->sym->name);
    return MCELL_FAIL;
  }
  free(rx_name);

  pathp = (struct pathway *)CHECKED_MALLOC_STRUCT(struct pathway,
                                                  "reaction pathway");
  if (pathp == NULL)
    return MCELL_FAIL;
  memset(pathp, 0, sizeof(struct pathway));

  rxnp = (struct rxn *)stp3->value;
  rxnp->n_reactants = 2;
  ++rxnp->n_pathways;
  pathp->pathname = NULL;
  pathp->reactant1 = surface_class;
  pathp->reactant2 = (struct species *)mol_sym->value;
  pathp->reactant3 = NULL;
  pathp->is_complex[0] = pathp->is_complex[1] = pathp->is_complex[2] = 0;
  pathp->flags = 0;

  pathp->flags |= PATHW_CLAMP_CONC;

  pathp->km = conc;
  pathp->km_filename = NULL;
  pathp->km_complex = NULL;

  pathp->orientation1 = 1;
  pathp->orientation3 = 0;
  if (orient == 0) {
    pathp->orientation2 = 0;
  } else {
    pathp->orientation2 = (orient < 0) ? -1 : 1;
  }

  pathp->product_head = NULL;
  pathp->prod_signature = NULL;

  pathp->next = rxnp->pathway_head;
  rxnp->pathway_head = pathp;

  no = CHECKED_MALLOC_STRUCT(struct name_orient, "struct name_orient");
  no->name = CHECKED_STRDUP(mol_sym->name, "molecule name");
  no->orient = pathp->orientation2;

  if (surface_class->clamp_conc_mols == NULL) {
    no->next = NULL;
    surface_class->clamp_conc_mols = no;
  } else {
    no->next = surface_class->clamp_conc_mols;
    surface_class->clamp_conc_mols = no;
  }

  return MCELL_SUCCESS;
}

/**************************************************************************
 * What follows are API functions for adding model elements independent of the
 * parser
 **************************************************************************/

/*************************************************************************
 mcell_create_species:
    Create a new species. This uses the same helper functions as the parser,
    but is meant to be used independent of the parser.

 In: state: the simulation state
     name:  molecule name
     D:     diffusion constant
     D_ref: reference diffusion constant
     is_2d: 1 if the species is a 2D molecule, 0 if 3D
     custom_time_step: time_step for the molecule (< 0.0 for a custom space
                       step, >0.0 for custom timestep, 0.0 for default
                       timestep)
     target_only: 1 if the molecule cannot initiate reactions
     max_step_length:
 Out: Returns 0 on sucess and 1 on error
*************************************************************************/
MCELL_STATUS
mcell_create_species(MCELL_STATE *state, struct mcell_species_spec *species,
                     mcell_symbol **species_ptr) {
  struct sym_table *sym =
      CHECKED_MALLOC_STRUCT(struct sym_table, "sym table entry");
  int error_code = new_mol_species(state, species->name, sym);
  if (error_code) {
    return error_code;
  }

  // Perhaps we should consider getting rid of D_ref. It doesn't seem to be
  // used for anything important. Need to rip it out of test suite first.
  assemble_mol_species(state, sym, species);

  error_code = ensure_rdstep_tables_built(state);
  if (error_code) {
    return error_code;
  }

  if (species_ptr != NULL) {
    *species_ptr = sym;
  }

  return MCELL_SUCCESS;
}

/*************************************************************************
 mcell_set_iterations:
    Set the number of iterations for the simulation.

 In: state: the simulation state
     iterations: number of iterations to run
 Out: 0 on success; 1 on failure.
      number of iterations is set.
*************************************************************************/
MCELL_STATUS
mcell_set_iterations(MCELL_STATE *state, long long iterations) {
  if (iterations < 0) {
    return MCELL_FAIL;
  }
  state->iterations = iterations;
  return MCELL_SUCCESS;
}

/*************************************************************************
 mcell_set_time_step:
    Set the global timestep for the simulation.

 In: state: the simulation state
      step: timestep to set
 Out: 0 on success; any other integer value is a failure.
      global timestep is updated.
*************************************************************************/
MCELL_STATUS
mcell_set_time_step(MCELL_STATE *state, double step) {
  if (step <= 0) {
    return 2;
  }
  // Timestep was already set. Could introduce subtle problems if we let it
  // change after defining the species, since it is used in calculations there.
  if (state->time_unit != 0) {
    return 3;
  }
  state->time_unit = step;
  return MCELL_SUCCESS;
}

/*************************************************************************
 mcell_create_meta_object:
  Create a new meta object.

 In: state:    the simulation state
               object pointer to store created meta object
 Out: 0 on success; any other integer value is a failure.
      A mesh is created.
*************************************************************************/
MCELL_STATUS
mcell_create_instance_object(MCELL_STATE *state, char *name,
                             struct object **new_object) {
  // Create the symbol, if it doesn't exist yet.
  struct object *obj_ptr = make_new_object(state, name);
  if (obj_ptr == NULL) {
    return MCELL_FAIL;
  }
  obj_ptr->last_name = name;
  obj_ptr->object_type = META_OBJ;

  // instantiate object
  obj_ptr->parent = state->root_instance;
  add_child_objects(state->root_instance, obj_ptr, obj_ptr);

  *new_object = obj_ptr;

  return MCELL_SUCCESS;
}

/*************************************************************************
 mcell_create_poly_object:
  Create a new polygon object.

 In: state:    the simulation state
     poly_obj: all the information needed to create the polygon object (name,
               vertices, connections)
 Out: 0 on success; any other integer value is a failure.
      A mesh is created.
*************************************************************************/
MCELL_STATUS
mcell_create_poly_object(MCELL_STATE *state, struct object *parent,
                         struct poly_object *poly_obj,
                         struct object **new_object) {
  // create qualified object name
  char *qualified_name =
      CHECKED_SPRINTF("%s.%s", parent->sym->name, poly_obj->obj_name);

  // Create the symbol, if it doesn't exist yet.
  struct object *obj_ptr = make_new_object(state, qualified_name);
  if (obj_ptr == NULL) {
    return MCELL_FAIL;
  }
  obj_ptr->last_name = qualified_name;

  // Create the actual polygon object
  new_polygon_list(state, obj_ptr, poly_obj->num_vert, poly_obj->vertices,
                   poly_obj->num_conn, poly_obj->connections);

  // Do some clean-up.
  remove_gaps_from_regions(obj_ptr);
  if (check_degenerate_polygon_list(obj_ptr)) {
    return MCELL_FAIL;
  }

  // Set the parent of the object to be the root object. Not reciprocal until
  // add_child_objects is called.
  obj_ptr->parent = parent;
  add_child_objects(parent, obj_ptr, obj_ptr);

  *new_object = obj_ptr;

  return MCELL_SUCCESS;
}

/*************************************************************************
 make_new_object:
    Create a new object, adding it to the global symbol table.

 In:  state: system state
      obj_name: fully qualified object name
 Out: the newly created object
*************************************************************************/
struct object *
make_new_object(MCELL_STATE *state, char *obj_name) {
  if ((retrieve_sym(obj_name, state->obj_sym_table)) != NULL) {
    // mdlerror_fmt(parse_state,"Object '%s' is already defined", obj_name);
    return NULL;
  }

  struct sym_table *symbol;
  if ((symbol = store_sym(obj_name, OBJ, state->obj_sym_table, NULL)) == NULL) {
    return NULL;
  }

  return (struct object *)symbol->value;
}

/**************************************************************************
 *
 * The following functions are likely too low-level to be a part of the API.
 * However they are currently needed by the parser. Eventually, we should
 * try to merge these into other higher-level functions.
 *
 **************************************************************************/

/*************************************************************************
 start_object:
    Create a new object, adding it to the global symbol table. The object must
    not be defined yet. The qualified name of the object will be built by
    adding to the object_name_list, and the object is made the "current_object"
    in the mdl parser state. Because of these side effects, it is vital to call
    finish_object at the end of the scope of the object created here.

 In:  state: the simulation state
      obj_creation: information about object being created
      name: unqualified object name
 Out: the newly created object
 NOTE: This is very similar to mdl_start_object, but there is no parse state.
*************************************************************************/
struct object *
start_object(MCELL_STATE *state,
             struct object_creation *obj_creation, char *name) {
  // Create new fully qualified name.
  char *new_name;
  if ((new_name = push_object_name(obj_creation, name)) == NULL) {
    free(name);
    return NULL;
  }

  // Create the symbol, if it doesn't exist yet.
  struct object *obj_ptr = make_new_object(state, new_name);
  if (obj_ptr == NULL) {
    free(name);
    free(new_name);
    return NULL;
  }

  obj_ptr->last_name = name;
  no_printf("Creating new object: %s\n", new_name);

  // Set parent object, make this object "current".
  obj_ptr->parent = obj_creation->current_object;

  return obj_ptr;
}

/**************************************************************************
 new_polygon_list:
    Create a new polygon list object.

 In: state: the simulation state
     obj_ptr: contains information about the object (name, etc)
     n_vertices: count of vertices
     vertices: list of vertices
     n_connections: count of walls
     connections: list of walls
 Out: polygon object, or NULL if there was an error
 NOTE: This is similar to mdl_new_polygon_list
**************************************************************************/
struct polygon_object *
new_polygon_list(MCELL_STATE *state, struct object *obj_ptr, int n_vertices,
                 struct vertex_list *vertices, int n_connections,
                 struct element_connection_list *connections) {

  struct polygon_object *poly_obj_ptr =
      allocate_polygon_object("polygon list object");
  if (poly_obj_ptr == NULL) {
    goto failure;
  }

  obj_ptr->object_type = POLY_OBJ;
  obj_ptr->contents = poly_obj_ptr;

  poly_obj_ptr->n_walls = n_connections;
  poly_obj_ptr->n_verts = n_vertices;

  // Allocate and initialize removed sides bitmask
  poly_obj_ptr->side_removed = new_bit_array(poly_obj_ptr->n_walls);
  if (poly_obj_ptr->side_removed == NULL) {
    goto failure;
  }
  set_all_bits(poly_obj_ptr->side_removed, 0);

  // Keep temporarily information about vertices in the form of
  // "parsed_vertices"
  poly_obj_ptr->parsed_vertices = vertices;

  // Copy in vertices and normals
  struct vertex_list *vert_list = poly_obj_ptr->parsed_vertices;
  for (int i = 0; i < poly_obj_ptr->n_verts; i++) {
    // Rescale vertices coordinates
    vert_list->vertex->x *= state->r_length_unit;
    vert_list->vertex->y *= state->r_length_unit;
    vert_list->vertex->z *= state->r_length_unit;

    vert_list = vert_list->next;
  }

  // Allocate wall elements
  struct element_data *elem_data_ptr = NULL;
  if ((elem_data_ptr =
           CHECKED_MALLOC_ARRAY(struct element_data, poly_obj_ptr->n_walls,
                                "polygon list object walls")) == NULL) {
    goto failure;
  }
  poly_obj_ptr->element = elem_data_ptr;

  // Copy in wall elements
  for (int i = 0; i < poly_obj_ptr->n_walls; i++) {
    if (connections->n_verts != 3) {
      // mdlerror(parse_state, "All polygons must have three vertices.");
      goto failure;
    }

    struct element_connection_list *elem_conn_list_temp = connections;
    memcpy(elem_data_ptr[i].vertex_index, connections->indices,
           3 * sizeof(int));
    connections = connections->next;
    free(elem_conn_list_temp->indices);
    free(elem_conn_list_temp);
  }

  // Create object default region on polygon list object:
  struct region *reg_ptr = NULL;
  if ((reg_ptr = create_region(state, obj_ptr, "ALL")) == NULL) {
    goto failure;
  }
  if ((reg_ptr->element_list_head =
           new_element_list(0, poly_obj_ptr->n_walls - 1)) == NULL) {
    goto failure;
  }

  obj_ptr->n_walls = poly_obj_ptr->n_walls;
  obj_ptr->n_verts = poly_obj_ptr->n_verts;
  if (normalize_elements(reg_ptr, 0)) {
    // mdlerror_fmt(parse_state,
    //             "Error setting up elements in default 'ALL' region in the "
    //             "polygon object '%s'.", sym->name);
    goto failure;
  }

  return poly_obj_ptr;

failure:
  free_connection_list(connections);
  free_vertex_list(vertices);
  if (poly_obj_ptr) {
    if (poly_obj_ptr->element) {
      free(poly_obj_ptr->element);
    }
    if (poly_obj_ptr->side_removed) {
      free_bit_array(poly_obj_ptr->side_removed);
    }
    free(poly_obj_ptr);
  }
  return NULL;
}

/**************************************************************************
 finish_polygon_list:
    Finalize the polygon list, cleaning up any state updates that were made
    when we started creating the polygon.

 In: obj_ptr: contains information about the object (name, etc)
     obj_creation: information about object being created
 Out: 1 on failure, 0 on success
 NOTE: This function call might be too low-level for what we want from the API,
       but it is needed to create polygon objects for now.
**************************************************************************/
int finish_polygon_list(struct object *obj_ptr,
                        struct object_creation *obj_creation) {
  pop_object_name(obj_creation);
  remove_gaps_from_regions(obj_ptr);
  // no_printf(" n_verts = %d\n", mpvp->current_polygon->n_verts);
  // no_printf(" n_walls = %d\n", mpvp->current_polygon->n_walls);
  if (check_degenerate_polygon_list(obj_ptr)) {
    return 1;
  }
  return 0;
}

/**************************************************************************
 start_release_site:
    Start parsing the innards of a release site.

 In: state: system state
     sym_ptr: symbol for the release site
 Out: 0 on success, 1 on failure
**************************************************************************/
MCELL_STATUS
mcell_start_release_site(MCELL_STATE *state, struct sym_table *sym_ptr,
                         struct object **obj) {
  struct object *obj_ptr = (struct object *)sym_ptr->value;
  obj_ptr->object_type = REL_SITE_OBJ;
  obj_ptr->contents = new_release_site(state, sym_ptr->name);
  if (obj_ptr->contents == NULL) {
    return MCELL_FAIL;
  }

  *obj = obj_ptr;

  return MCELL_SUCCESS;
}

/**************************************************************************
 finish_release_site:
    Finish parsing the innards of a release site.

 In: sym_ptr: symbol for the release site
 Out: the object, on success, or NULL on failure
**************************************************************************/
MCELL_STATUS
mcell_finish_release_site(struct sym_table *sym_ptr, struct object **obj) {
  struct object *obj_ptr_new = (struct object *)sym_ptr->value;
  no_printf("Release site %s defined:\n", sym_ptr->name);
  if (is_release_site_valid((struct release_site_obj *)obj_ptr_new->contents)) {
    return MCELL_FAIL;
  }
  *obj = obj_ptr_new;

  return MCELL_SUCCESS;
}

/***************** MARKUS *********************************************
 * new release site stuff
 ***********************************************************************/
MCELL_STATUS
mcell_create_geometrical_release_site(
    MCELL_STATE *state, struct object *parent, char *site_name, int shape,
    struct vector3 *position, struct vector3 *diameter,
    struct mcell_species *mol, double num_molecules, double rel_prob,
    char *pattern_name, struct object **new_object) {
  assert(shape != SHAPE_REGION && shape != SHAPE_LIST);
  assert((((struct species *)mol->mol_type->value)->flags & NOT_FREE) == 0);

  // create qualified object name
  char *qualified_name = CHECKED_SPRINTF("%s.%s", parent->sym->name, site_name);

  struct object *release_object = make_new_object(state, qualified_name);
  // release_object->parent = state->root_instance;

  // Set the parent of the object to be the root object. Not reciprocal until
  // add_child_objects is called.
  release_object->parent = parent;
  add_child_objects(parent, release_object, release_object);

  struct object *dummy = NULL;
  mcell_start_release_site(state, release_object->sym, &dummy);

  // release site geometry and locations
  struct release_site_obj *releaser =
      (struct release_site_obj *)release_object->contents;
  releaser->release_shape = shape;
  set_release_site_location(state, releaser, position);

  releaser->diameter =
      CHECKED_MALLOC_STRUCT(struct vector3, "release site diameter");
  if (releaser->diameter == NULL) {
    return MCELL_FAIL;
  }
  releaser->diameter->x = diameter->x * state->r_length_unit;
  releaser->diameter->y = diameter->y * state->r_length_unit;
  releaser->diameter->z = diameter->z * state->r_length_unit;

  // release probability and release patterns
  if (rel_prob < 0 || rel_prob > 1) {
    return MCELL_FAIL;
  }

  if (pattern_name != NULL) {
    struct sym_table *symp = retrieve_sym(pattern_name, state->rpat_sym_table);
    if (symp == NULL) {
      symp = retrieve_sym(pattern_name, state->rxpn_sym_table);
      if (symp == NULL) {
        return MCELL_FAIL;
      }
    }
    releaser->pattern = (struct release_pattern *)symp->value;
    releaser->release_prob = MAGIC_PATTERN_PROBABILITY;
  } else {
    releaser->release_prob = rel_prob;
  }

  /* molecule and molecule number */
  set_release_site_constant_number(releaser, num_molecules);
  releaser->mol_type = (struct species *)mol->mol_type->value;
  releaser->orientation = mol->orient;

  mcell_finish_release_site(release_object->sym, &dummy);

  *new_object = release_object;
  return MCELL_SUCCESS;
}

/*************************************************************************
 In: state: system state
     rel_site_obj_ptr: the release site object to validate
     obj_ptr: the object representing this release site
     rel_eval: the release evaluator representing the region of release
 Out: 0 on success, 1 on failure
**************************************************************************/
int mcell_set_release_site_geometry_region(
    MCELL_STATE *state, struct release_site_obj *rel_site_obj_ptr,
    struct object *obj_ptr, struct release_evaluator *rel_eval) {

  rel_site_obj_ptr->release_shape = SHAPE_REGION;
  state->place_waypoints_flag = 1;

  struct release_region_data *rel_reg_data = CHECKED_MALLOC_STRUCT(
      struct release_region_data, "release site on region");
  if (rel_reg_data == NULL) {
    return 1;
  }

  rel_reg_data->n_walls_included = -1; /* Indicates uninitialized state */
  rel_reg_data->cum_area_list = NULL;
  rel_reg_data->wall_index = NULL;
  rel_reg_data->obj_index = NULL;
  rel_reg_data->n_objects = -1;
  rel_reg_data->owners = NULL;
  rel_reg_data->in_release = NULL;
  rel_reg_data->self = obj_ptr;

  rel_reg_data->expression = rel_eval;

  if (check_release_regions(rel_eval, obj_ptr, state->root_instance)) {
    // Trying to release on a region that the release site cannot see! Try
    // grouping the release site and the corresponding geometry with an OBJECT.
    free(rel_reg_data);
    return 2;
  }

  rel_site_obj_ptr->region_data = rel_reg_data;
  return 0;
}

/****************************************************************
 * routines for manipulating visualization data output
 ****************************************************************/

/*************************************************************************
 mcell_create_viz_output:
    Create a new set of viz output.

 In:  state: MCell state
      filename: the path and filename prefix where the viz data will be
                (e.g. "./viz_data/my_viz")
      mol_viz_list: a list of the molecules to be visualized
      start: the first frame of the viz data
      end: the last frame of the viz data
      step: the delta between iterations
 Out: Returns 1 on error and 0 on success
 Note: Right now, only iterations (not time points) can be specified.
*************************************************************************/
MCELL_STATUS
mcell_create_viz_output(MCELL_STATE *state, char *filename,
                        struct mcell_species *mol_viz_list,
                        long long start,
                        long long end,
                        long long step) {

  struct viz_output_block *vizblk = CHECKED_MALLOC_STRUCT(
      struct viz_output_block, "visualization data output parameters");
  if (vizblk == NULL)
    return MCELL_FAIL;

  mcell_new_viz_output_block(vizblk);
  // In principal, it's possible to have multiple viz blocks (one for each
  // mode, e.g. CELLBLENDER, DREAMM_V3), but this isn't supported in the API
  // yet.
  vizblk->next = state->viz_blocks;
  state->viz_blocks = vizblk;

  // Only CELLBLENDER mode is supported right now
  vizblk->viz_mode = CELLBLENDER_MODE;

  // Set the viz output path and filename prefix
  vizblk->file_prefix_name = filename;
  if (vizblk->molecule_prefix_name == NULL)
    vizblk->molecule_prefix_name = filename;

  // Select which molecules will be visualized
  if (select_viz_molecules(mol_viz_list, vizblk))
    return MCELL_FAIL;

  // Select which iterations will be visualized
  struct frame_data_list *new_frame = create_viz_frame(
    state->iterations, start, end, step);
  if (new_frame == NULL)
    return MCELL_FAIL;

  new_frame->next = NULL;
  state->viz_blocks->frame_data_head = new_frame;
  
  return MCELL_SUCCESS;
}

/**************************************************************************
 mcell_new_viz_output_block:
    Build a new VIZ output block, containing parameters for an output set for
    visualization.
**************************************************************************/
void
mcell_new_viz_output_block(struct viz_output_block *vizblk) {
  vizblk->frame_data_head = NULL;
  memset(&vizblk->viz_state_info, 0, sizeof(vizblk->viz_state_info));
  vizblk->viz_mode = -1;
  vizblk->molecule_prefix_name = NULL;
  vizblk->file_prefix_name = NULL;
  vizblk->viz_output_flag = 0;
  vizblk->species_viz_states = NULL;

  vizblk->dreamm_object_info = NULL;
  vizblk->dreamm_objects = NULL;
  vizblk->n_dreamm_objects = 0;

  vizblk->dx_obj_head = NULL;
  vizblk->viz_children = init_symtab(1024);
  if (pointer_hash_init(&vizblk->parser_species_viz_states, 32))
    mcell_allocfailed("Failed to initialize viz species states table.");
}

/**************************************************************************
 mcell_mcell_create_viz_frame:
    Create a frame for output in the visualization.

 In: time_type: either OUTPUT_BY_TIME_LIST or OUTPUT_BY_ITERATION_LIST
     type: the type (MESH_GEOMETRY, MOL_POS, etc.)
     iteration_list: list of iterations/times at which to output
 Out: the frame_data_list object, if successful, or NULL if we ran out of
      memory
**************************************************************************/
struct frame_data_list *
mcell_create_viz_frame(int time_type, int type,
                       struct num_expr_list *iteration_list) {

  struct frame_data_list *fdlp;
  fdlp = CHECKED_MALLOC_STRUCT(struct frame_data_list, "VIZ_OUTPUT frame data");
  if (fdlp == NULL)
    return NULL;

  fdlp->list_type = time_type;
  fdlp->type = type;
  fdlp->viz_iteration = -1;
  fdlp->n_viz_iterations = 0;
  fdlp->iteration_list = iteration_list;
  fdlp->curr_viz_iteration = iteration_list;
  return fdlp;
}

/**************************************************************************
 mcell_set_molecule_viz_state:
    Sets a flag on a viz block, requesting that a molecule is visualized.

 In: vizblk: the viz block to check
     specp: the molecule species
     viz_state: the desired viz state
 Out: 0 on success, 1 on failure
**************************************************************************/
MCELL_STATUS
mcell_set_molecule_viz_state(struct viz_output_block *vizblk,
                             struct species *specp, int viz_state) {

  /* Make sure not to override a specific state with a generic state. */
  if (viz_state == INCLUDE_OBJ) {
    void *const exclude = (void *)(intptr_t)EXCLUDE_OBJ;

    void *oldval = pointer_hash_lookup_ext(&vizblk->parser_species_viz_states,
                                           specp, specp->hashval, exclude);
    if (oldval != exclude)
      return 0;
  } else
    vizblk->viz_output_flag |= VIZ_MOLECULES_STATES;

  /* Store new value in the hashtable or die trying. */
  void *val = (void *)(intptr_t)viz_state;
  assert(viz_state == (int)(intptr_t)val);
  if (pointer_hash_add(&vizblk->parser_species_viz_states, specp,
                       specp->hashval, val)) {
    mcell_allocfailed(
        "Failed to store viz state for molecules of species '%s'.",
        specp->sym->name);
    return MCELL_FAIL;
  }
  return MCELL_SUCCESS;
}

/****************************************************************
 * routines for manipulating reaction data output
 ****************************************************************/

/*************************************************************************
 mcell_new_output_request:
    Create a new output request.

 In:  state: MCell state
      target: what are we counting
      orientation: how is it oriented?
      location: where are we counting?
      report_flags: what type of events are we counting?
 Out: output request item, or NULL if an error occurred
*************************************************************************/
struct output_request *
mcell_new_output_request(MCELL_STATE *state, struct sym_table *target,
  short orientation, struct sym_table *location, int report_flags) {
  struct output_request *orq;
  struct output_expression *oe;

  orq = CHECKED_MEM_GET(state->outp_request_mem, "count request");
  if (orq == NULL)
    return NULL;

  oe = new_output_expr(state->oexpr_mem);
  if (oe == NULL) {
    mem_put(state->outp_request_mem, orq);
    mcell_allocfailed("Failed to allocate a count expression.");
    return NULL;
  }
  orq->next = NULL;
  orq->requester = oe;
  orq->count_target = target;
  orq->count_orientation = orientation;
  orq->count_location = location;
  orq->report_type = report_flags;

  oe->left = orq;
  oe->oper = '#';
  oe->expr_flags = OEXPR_LEFT_REQUEST;
  if (orq->report_type & REPORT_TRIGGER)
    oe->expr_flags |= OEXPR_TYPE_TRIG;
  else if ((orq->report_type & REPORT_TYPE_MASK) != REPORT_CONTENTS)
    oe->expr_flags |= OEXPR_TYPE_DBL;
  else
    oe->expr_flags |= OEXPR_TYPE_INT;
  return orq;
}


/******************************************************************************
 *
 * mcell_create_count creates a single count expression and returns it as a
 * output_column_list.
 * Inputs are:
 *    - symbol table entry for target (molecule or reaction)
 *    - orientation for molecule counts
 *    - symbol table entry for count location (NULL implies WORLD)
 *    - report flags (REPORT_WORLD, REPORT_CONTENTS, ...)
 *    - custom header (or NULL if not wanted)
 *    - pointer to empty count list to which count expression will be added
 *
 *****************************************************************************/
MCELL_STATUS
mcell_create_count(MCELL_STATE *state, struct sym_table *target,
  short orientation, struct sym_table *location, int report_flags,
  char* custom_header, struct output_column_list *count_list) {

  struct output_request *output_A = NULL;
  if ((output_A = mcell_new_output_request(state, target, orientation,
    location, report_flags)) == NULL) {
    return MCELL_FAIL;
  }
  output_A->next = state->output_request_head;
  state->output_request_head = output_A;

  return mcell_prepare_single_count_expr(count_list, output_A->requester,
    custom_header);
}


/*************************************************************************
 mcell_create_new_output_set
    Create a new output set. Here output set refers to a count/trigger
    block which goes to a single data output file.

 In:  state: MCell state
      comment: textual comment describing the data set or NULL
      exact_time: request exact_time output for trigger statements
      col_head: head of linked list of output columns
      file_flags: file creation flags for output file
      outfile_name: name of output file
 Out: output request item, or NULL if an error occurred
*************************************************************************/
struct output_set *
mcell_create_new_output_set(MCELL_STATE *state,
  char *comment, int exact_time, struct output_column *col_head,
  int file_flags, char *outfile_name) {

  struct output_set *os = CHECKED_MALLOC_STRUCT(struct output_set,
    "reaction data output set");
  if (os == NULL) {
    return NULL;
  }

  os->outfile_name = outfile_name;
  os->file_flags = file_flags;
  os->exact_time_flag = exact_time;
  os->chunk_count = 0;
  os->block = NULL;
  os->next = NULL;

  struct output_column *oc = col_head;
  os->column_head = oc;

  if (comment == NULL)
    os->header_comment = NULL;
  else if (comment[0] == '\0')
    os->header_comment = "";
  else {
    os->header_comment = strdup(comment);
    if (os->header_comment == NULL) {
      return NULL;
    }
    if (os->header_comment == NULL) {
      free(os);
      return NULL;
    }
  }

  for (; oc != NULL; oc = oc->next)
    oc->set = os;

  if (check_reaction_output_file(os))
    return NULL;

  return os;
}

/*****************************************************************************
 *
 * mcell_prepare_single_count_expression prepares a count expression for
 * inclusion in an output set
 *
 *****************************************************************************/
MCELL_STATUS
mcell_prepare_single_count_expr(struct output_column_list *list,
  struct output_expression *expr, char *custom_header)
{
  list->column_head = NULL;
  list->column_tail = NULL;

  if (custom_header != NULL) {
    expr->title = custom_header;
  }

  /* If we have a list of results, go through to build column stack */
  struct output_expression *oe;
  struct output_column *oc;
  for (oe = first_oexpr_tree(expr); oe != NULL; oe = next_oexpr_tree(oe)) {
    if ((oc = new_output_column()) == NULL)
      return MCELL_FAIL;

    if (!list->column_head)
      list->column_head = list->column_tail = oc;
    else
      list->column_tail = list->column_tail->next = oc;

    oc->expr = oe;
    set_oexpr_column(oe, oc);
  }

  return MCELL_SUCCESS;
}

/*****************************************************************************
 *
 * mcell_add_reaction_output_block creates a new reaction data output block
 * and adds it to the world.
 *
 *****************************************************************************/
MCELL_STATUS
mcell_add_reaction_output_block(MCELL_STATE *state,
  struct output_set_list *osets, int buffer_size,
  struct output_times_inlist *otimes) {

  struct output_block *obp;
  struct output_set *os;

  if ((obp = new_output_block(buffer_size)) == NULL)
    return 1;

  if (otimes->type == OUTPUT_BY_STEP)
    set_reaction_output_timer_step(state, obp, otimes->step);
  else if (otimes->type == OUTPUT_BY_ITERATION_LIST) {
    if (set_reaction_output_timer_iterations(state, obp, &otimes->values))
      return MCELL_FAIL;
  } else if (otimes->type == OUTPUT_BY_TIME_LIST) {
    if (set_reaction_output_timer_times(state, obp, &otimes->values))
      return MCELL_FAIL;
  } else {
    mcell_error("Internal error: Invalid output timer def (%d)", otimes->type);
    return MCELL_FAIL;
  }
  obp->data_set_head = osets->set_head;
  for (os = obp->data_set_head; os != NULL; os = os->next)
    os->block = obp;
  if (output_block_finalize(state, obp))
    return 1;
  obp->next = state->output_block_head;
  state->output_block_head = obp;
  return MCELL_SUCCESS;
}


/**************************************************************************
 *
 * what follows are helper functions *not* part of the actual API.
 *
 * XXX: Many of these functions should not be called from client
 *      code and need to be removed eventually.
 *
 **************************************************************************/

/***********************************************************************
 * install_usr_signal_handlers:
 *
 *   Set signal handlers for checkpointing on SIGUSR signals.
 *
 *   In:  None
 *   Out: 0 on success, 1 on failure.
 ***********************************************************************/
static int install_usr_signal_handlers(void) {
#ifndef _WIN32 /* fixme: Windows does not support USR signals */
  struct sigaction sa, saPrev;
  sa.sa_sigaction = NULL;
  sa.sa_handler = &chkpt_signal_handler;
  sa.sa_flags = SA_RESTART;
  sigfillset(&sa.sa_mask);

  if (sigaction(SIGUSR1, &sa, &saPrev) != 0) {
    mcell_error("Failed to install USR1 signal handler.");
    return 1;
  }
  if (sigaction(SIGUSR2, &sa, &saPrev) != 0) {
    mcell_error("Failed to install USR2 signal handler.");
    return 1;
  }
#endif

  return 0;
}

/************************************************************************
 *
 * mcell_print_version prints the version string
 *
 ************************************************************************/
void mcell_print_version() { print_version(mcell_get_log_file()); }

/************************************************************************
 *
 * mcell_print_usage prints the usage information
 *
 ************************************************************************/
void mcell_print_usage(const char *executable_name) {
  print_usage(mcell_get_log_file(), executable_name);
}

/************************************************************************
 *
 * mcell_print_stats prints the simulation stats
 *
 ************************************************************************/
void mcell_print_stats() { mem_dump_stats(mcell_get_log_file()); }

/************************************************************************
 *
 * function for printing a string
 *
 * XXX: This is a temporary hack to be able to print in mcell.c
 *      since mcell disables regular printf
 *
 ************************************************************************/
void mcell_print(const char *message) { mcell_log("%s", message); }

/************************************************************************
 *
 * mcell_argparse parses the commandline and sets the
 * corresponding parts of the state (seed #, logging, ...)
 *
 ************************************************************************/
int mcell_argparse(int argc, char **argv, MCELL_STATE *state) {
  return argparse_init(argc, argv, state);
}

/************************************************************************
 *
 * get_counter_trigger_column retrieves the output_column corresponding
 * to a given count or trigger statement.
 *
 ************************************************************************/
struct output_column *get_counter_trigger_column(MCELL_STATE *state,
                                                 const char *counter_name,
                                                 int column_id) {
  // retrieve the counter for the requested counter_name
  struct sym_table *counter_sym =
      retrieve_sym(counter_name, state->counter_by_name);
  if (counter_sym == NULL) {
    mcell_log("Failed to retrieve symbol for counter %s.", counter_name);
    return NULL;
  }
  struct output_set *counter = (struct output_set *)(counter_sym->value);

  // retrieve the requested column
  struct output_column *column = counter->column_head;
  int count = 0;
  while (count < column_id && column != NULL) {
    count++;
    column = column->next;
  }
  if (count != column_id || column == NULL) {
    return NULL;
  }

  return column;
}

/*****************************************************************************
 *
 * mcell_add_to_vertex_list creates a linked list of mesh vertices belonging
 * to a polygon object
 *
 * During the first invocation of this function, NULL should be provided for
 * vertices to initialize a new vertex list. On subsecquent invocations the
 * current vertex_list should be provided as parameter vertices to which the
 * new vertex will be appended.
 *
 *****************************************************************************/
struct vertex_list *mcell_add_to_vertex_list(double x, double y, double z,
                                             struct vertex_list *vertices) {
  struct vertex_list *verts = (struct vertex_list *)CHECKED_MALLOC_STRUCT(
      struct vertex_list, "vertex list");
  if (verts == NULL) {
    return NULL;
  }

  struct vector3 *v =
      (struct vector3 *)CHECKED_MALLOC_STRUCT(struct vector3, "vector");
  if (v == NULL) {
    return NULL;
  }
  v->x = x;
  v->y = y;
  v->z = z;

  verts->vertex = v;
  verts->next = vertices;

  return verts;
}

/*****************************************************************************
 *
 * mcell_add_to_connection_list creates a linked list of element connections
 * describing a polygon object.
 *
 * During the first invocation of this function, NULL should be provided for
 * elements to initialize a new element connection list. On subsecquent
 * invocations the current element_connection_list should be provided as
 * parameter elements to which the new element connection will be appended.
 *
 *****************************************************************************/
struct element_connection_list *
mcell_add_to_connection_list(int v1, int v2, int v3,
                             struct element_connection_list *elements) {
  struct element_connection_list *elems =
      (struct element_connection_list *)CHECKED_MALLOC_STRUCT(
          struct element_connection_list, "element connection list");
  if (elems == NULL) {
    return NULL;
  }

  int *e = (int *)CHECKED_MALLOC_ARRAY(int, 3, "element connections");
  if (e == NULL) {
    return NULL;
  }
  e[0] = v1;
  e[1] = v2;
  e[2] = v3;

  elems->n_verts = 3;
  elems->indices = e;
  elems->next = elements;

  return elems;
}

/*****************************************************************************
 *
 * mcell_add_to_species_list creates a linked list of mcell_species from
 * mcell_symbols.
 *
 * The list of mcell_species is for example used to provide the list
 * of reactants, products and surface classes needed for creating
 * reactions.
 *
 * During the first invocation of this function, NULL should be provided for
 * the species_list to initialize a new mcell_species list with mcell_symbol.
 * On subsecquent invocations the current mcell_species list should
 * be provided as species_list to which the new mcell_symbol will be appended
 * with the appropriate flags for orientation and subunit status.
 *
 *****************************************************************************/
struct mcell_species *
mcell_add_to_species_list(mcell_symbol *species_ptr, bool is_oriented,
                          int orientation, bool is_subunit,
                          struct mcell_species *species_list) {
  struct mcell_species *species = (struct mcell_species *)CHECKED_MALLOC_STRUCT(
      struct mcell_species, "species list");
  if (species == NULL) {
    return NULL;
  }

  species->next = NULL;
  species->mol_type = species_ptr;
  species->orient_set = 1 ? is_oriented : 0;
  species->orient = orientation;
  species->is_subunit = 1 ? is_subunit : 0;

  if (species_list != NULL) {
    species->next = species_list;
  }

  return species;
}

/*****************************************************************************
 *
 * mcell_delete_species_list frees all memory associated with a list of
 * mcell_species
 *
 *****************************************************************************/
void mcell_delete_species_list(struct mcell_species *species) {
  struct mcell_species *tmp = species;
  while (species) {
    tmp = species->next;
    free(species);
    species = tmp;
  }
}

/*****************************************************************************
 *
 * mcell_create_reaction_rates list creates a struct reaction_rates used
 * for creating reactions from a forward and backward reaction rate.
 * The backward rate is only needed for catalytic arrow and should be
 * RATE_UNUSED otherwise.
 *
 *****************************************************************************/
struct reaction_rates mcell_create_reaction_rates(int forwardRateType,
                                                  int forwardRateConstant,
                                                  int backwardRateType,
                                                  int backwardRateConstant) {
  struct reaction_rate forwardRate;
  forwardRate.rate_type = forwardRateType;
  forwardRate.v.rate_constant = forwardRateConstant;

  struct reaction_rate backwardRate;
  backwardRate.rate_type = backwardRateType;
  backwardRate.v.rate_constant = backwardRateConstant;

  struct reaction_rates rates = { forwardRate, backwardRate };

  return rates;
}

/***** merged from create_release_sites *******/

/**************************************************************************
 set_release_site_location:
    Set the location of a release site.

 In: state: system state
     rel_site_obj_ptr: release site
     location: location for release site
 Out: none
**************************************************************************/
void set_release_site_location(MCELL_STATE *state,
                               struct release_site_obj *rel_site_obj_ptr,
                               struct vector3 *location) {
  rel_site_obj_ptr->location = location;
  rel_site_obj_ptr->location->x *= state->r_length_unit;
  rel_site_obj_ptr->location->y *= state->r_length_unit;
  rel_site_obj_ptr->location->z *= state->r_length_unit;
}

/**************************************************************************
 set_release_site_constant_number:
    Set a constant release quantity from this release site, in units of
    molecules.

 In: rel_site_obj_ptr: the release site
     num:  count of molecules to release
 Out: none.  release site object is updated
**************************************************************************/
void set_release_site_constant_number(struct release_site_obj *rel_site_obj_ptr,
                                      double num) {
  rel_site_obj_ptr->release_number_method = CONSTNUM;
  rel_site_obj_ptr->release_number = num;
}

/**************************************************************************
 set_release_site_gaussian_number:
    Set a gaussian-distributed release quantity from this release site, in
    units of molecules.

 In: rel_site_obj_ptr: the release site
     mean: mean value of distribution
     stdev: std. dev. of distribution
 Out: none.  release site object is updated
**************************************************************************/
void set_release_site_gaussian_number(struct release_site_obj *rel_site_obj_ptr,
                                      double mean, double stdev) {
  rel_site_obj_ptr->release_number_method = GAUSSNUM;
  rel_site_obj_ptr->release_number = mean;
  rel_site_obj_ptr->standard_deviation = stdev;
}

/**************************************************************************
 set_release_site_geometry_region:
    Set the geometry for a particular release site to be a region expression.

 In: state: system state
     rel_site_obj_ptr: the release site object to validate
     obj_ptr: the object representing this release site
     rel_eval: the release evaluator representing the region of release
 Out: 0 on success, 1 on failure
**************************************************************************/
int set_release_site_geometry_region(MCELL_STATE *state,
                                     struct release_site_obj *rel_site_obj_ptr,
                                     struct object *obj_ptr,
                                     struct release_evaluator *rel_eval) {

  rel_site_obj_ptr->release_shape = SHAPE_REGION;
  state->place_waypoints_flag = 1;

  struct release_region_data *rel_reg_data = CHECKED_MALLOC_STRUCT(
      struct release_region_data, "release site on region");
  if (rel_reg_data == NULL) {
    return 1;
  }

  rel_reg_data->n_walls_included = -1; /* Indicates uninitialized state */
  rel_reg_data->cum_area_list = NULL;
  rel_reg_data->wall_index = NULL;
  rel_reg_data->obj_index = NULL;
  rel_reg_data->n_objects = -1;
  rel_reg_data->owners = NULL;
  rel_reg_data->in_release = NULL;
  rel_reg_data->self = obj_ptr;

  rel_reg_data->expression = rel_eval;

  if (check_release_regions(rel_eval, obj_ptr, state->root_instance)) {
    // Trying to release on a region that the release site cannot see! Try
    // grouping the release site and the corresponding geometry with an OBJECT.
    free(rel_reg_data);
    return 2;
  }

  rel_site_obj_ptr->region_data = rel_reg_data;
  return 0;
}

/**************************************************************************
 new_release_region_expr_binary:
    Set the geometry for a particular release site to be a region expression.

 In: parse_state: parser state
     reL:  release evaluation tree (set operations) for left side of expression
     reR:  release evaluation tree for right side of expression
     op:   flags indicating the operation performed by this node
 Out: the release expression, or NULL if an error occurs
**************************************************************************/
struct release_evaluator *
new_release_region_expr_binary(struct release_evaluator *rel_eval_L,
                               struct release_evaluator *rel_eval_R, int op) {
  return pack_release_expr(rel_eval_L, rel_eval_R, op);
}

/*************************************************************************
 check_release_regions:

 In: state:    system state
     rel_eval: an release evaluator (set operations applied to regions)
     parent:   the object that owns this release evaluator
     instance: the root object that begins the instance tree
 Out: 0 if all regions refer to instanced objects or to a common ancestor of
      the object with the evaluator, meaning that the object can be found. 1 if
      any referred-to region cannot be found.
*************************************************************************/
int check_release_regions(struct release_evaluator *rel_eval,
                          struct object *parent, struct object *instance) {
  struct object *obj_ptr;

  if (rel_eval->left != NULL) {
    if (rel_eval->op & REXP_LEFT_REGION) {
      obj_ptr =
          common_ancestor(parent, ((struct region *)rel_eval->left)->parent);
      if (obj_ptr == NULL || (obj_ptr->parent == NULL && obj_ptr != instance)) {
        obj_ptr = common_ancestor(instance,
                                  ((struct region *)rel_eval->left)->parent);
      }

      if (obj_ptr == NULL) {
        // Region neither instanced nor grouped with release site
        return 2;
      }
    } else if (check_release_regions(rel_eval->left, parent, instance)) {
      return 1;
    }
  }

  if (rel_eval->right != NULL) {
    if (rel_eval->op & REXP_RIGHT_REGION) {
      obj_ptr =
          common_ancestor(parent, ((struct region *)rel_eval->right)->parent);
      if (obj_ptr == NULL || (obj_ptr->parent == NULL && obj_ptr != instance)) {
        obj_ptr = common_ancestor(instance,
                                  ((struct region *)rel_eval->right)->parent);
      }

      if (obj_ptr == NULL) {
        // Region not grouped with release site.
        return 3;
      }
    } else if (check_release_regions(rel_eval->right, parent, instance)) {
      return 1;
    }
  }

  return 0;
}

/**************************************************************************
 is_release_site_valid:
    Validate a release site.

 In: rel_site_obj_ptr: the release site object to validate
 Out: 0 if it is valid, 1 if not
**************************************************************************/
int is_release_site_valid(struct release_site_obj *rel_site_obj_ptr) {
  // Unless it's a list release, user must specify MOL type
  if (rel_site_obj_ptr->release_shape != SHAPE_LIST) {
    // Must specify molecule to release using MOLECULE=molecule_name.
    if (rel_site_obj_ptr->mol_type == NULL) {
      return 2;
    }

    // Make sure it's not a surface class
    if ((rel_site_obj_ptr->mol_type->flags & IS_SURFACE) != 0) {
      return 3;
    }
  }

  /* Check that concentration/density status of release site agrees with
   * volume/grid status of molecule */
  if (rel_site_obj_ptr->release_number_method == CCNNUM) {
    // CONCENTRATION may only be used with molecules that can diffuse in 3D.
    if ((rel_site_obj_ptr->mol_type->flags & NOT_FREE) != 0) {
      return 4;
    }
  } else if (rel_site_obj_ptr->release_number_method == DENSITYNUM) {
    // DENSITY may only be used with molecules that can diffuse in 2D.
    if ((rel_site_obj_ptr->mol_type->flags & NOT_FREE) == 0) {
      return 5;
    }
  }

  /* Molecules can only be removed via a region release */
  if (rel_site_obj_ptr->release_shape != SHAPE_REGION &&
      rel_site_obj_ptr->release_number < 0) {
    return 2;
  }

  /* Unless it's a region release we must have a location */
  if (rel_site_obj_ptr->release_shape != SHAPE_REGION) {
    if (rel_site_obj_ptr->location == NULL) {
      // Release site is missing location.
      if (rel_site_obj_ptr->release_shape != SHAPE_LIST ||
          rel_site_obj_ptr->mol_list == NULL) {
        return 6;
      } else {
        // Give it a default location of (0, 0, 0)
        rel_site_obj_ptr->location =
            CHECKED_MALLOC_STRUCT(struct vector3, "release site location");
        if (rel_site_obj_ptr->location == NULL)
          return 1;
        rel_site_obj_ptr->location->x = 0;
        rel_site_obj_ptr->location->y = 0;
        rel_site_obj_ptr->location->z = 0;
      }
    }
    no_printf("\tLocation = [%f,%f,%f]\n", rel_site_obj_ptr->location->x,
              rel_site_obj_ptr->location->y, rel_site_obj_ptr->location->z);
  }
  return 0;
}

/**************************************************************************
 set_release_site_concentration:
    Set a release quantity from this release site based on a fixed
    concentration within the release-site's area.

 In: rel_site_obj_ptr: the release site
     conc: concentration for release
 Out: 0 on success, 1 on failure.  release site object is updated
**************************************************************************/
int set_release_site_concentration(struct release_site_obj *rel_site_obj_ptr,
                                   double conc) {
  if (rel_site_obj_ptr->release_shape == SHAPE_SPHERICAL_SHELL) {
    return 1;
  }
  rel_site_obj_ptr->release_number_method = CCNNUM;
  rel_site_obj_ptr->concentration = conc;
  return 0;
}

/**************************************************************************
 mdl_new_release_region_expr_term:
    Create a new "release on region" expression term.

 In: my_sym: the symbol for the region comprising this term in the expression
 Out: the release evaluator on success, or NULL if allocation fails
**************************************************************************/
struct release_evaluator *
new_release_region_expr_term(struct sym_table *my_sym) {

  struct release_evaluator *rel_eval =
      CHECKED_MALLOC_STRUCT(struct release_evaluator, "release site on region");
  if (rel_eval == NULL) {
    return NULL;
  }

  rel_eval->op = REXP_NO_OP | REXP_LEFT_REGION;
  rel_eval->left = my_sym->value;
  rel_eval->right = NULL;

  ((struct region *)rel_eval->left)->flags |= COUNT_CONTENTS;
  return rel_eval;
}


/*************************************************************************
 mcell_copysort_numeric_list:
    Copy and sort a num_expr_list in ascending numeric order.

 In:  parse_state:  parser state
      head:  the list to sort
 Out: list is sorted
*************************************************************************/
struct num_expr_list * mcell_copysort_numeric_list(struct num_expr_list *head) {
  struct num_expr_list_head new_head;
  if (mcell_generate_range_singleton(&new_head, head->value))
    return NULL;

  head = head->next;
  while (head != NULL) {
    struct num_expr_list *insert_pt, **prev;
    for (insert_pt = new_head.value_head, prev = &new_head.value_head;
         insert_pt != NULL;
         prev = &insert_pt->next, insert_pt = insert_pt->next) {
      if (insert_pt->value >= head->value)
        break;
    }

    struct num_expr_list *new_item =
        CHECKED_MALLOC_STRUCT(struct num_expr_list, "numeric array");
    if (new_item == NULL) {
      mcell_free_numeric_list(new_head.value_head);
      return NULL;
    }

    new_item->next = insert_pt;
    new_item->value = head->value;
    *prev = new_item;
    if (insert_pt == NULL)
      new_head.value_tail = new_item;
    head = head->next;
  }

  return new_head.value_head;
}

/*************************************************************************
 mcell_sort_numeric_list:
    Sort a num_expr_list in ascending numeric order.  N.B. This uses bubble
    sort, which is O(n^2).  Don't use it if you expect your list to be very
    long.  The list is sorted in-place.

 In:  head:  the list to sort
 Out: list is sorted
*************************************************************************/
void mcell_sort_numeric_list(struct num_expr_list *head) {
  struct num_expr_list *curr, *next;
  int done = 0;
  while (!done) {
    done = 1;
    curr = head;
    while (curr != NULL) {
      next = curr->next;
      if (next != NULL) {
        if (curr->value > next->value) {
          done = 0;
          swap_double(&curr->value, &next->value);
        }
      }
      curr = next;
    }
  }
}


/*************************************************************************
 mcell_free_numeric_list:
    Free a num_expr_list.

 In:  nel:  the list to free
 Out: all elements are freed
*************************************************************************/
void mcell_free_numeric_list(struct num_expr_list *nel) {
  while (nel != NULL) {
    struct num_expr_list *n = nel;
    nel = nel->next;
    free(n);
  }
}


/*************************************************************************
 mcell_generate_range_singleton:
    Generate a numeric list containing a single value.

 In:  lh:   list to receive value
      value: value for list
 Out: 0 on success, 1 on failure
*************************************************************************/
int mcell_generate_range_singleton(struct num_expr_list_head *lh, double value) {

  struct num_expr_list *nel =
      CHECKED_MALLOC_STRUCT(struct num_expr_list, "numeric array");
  if (nel == NULL)
    return 1;
  lh->value_head = lh->value_tail = nel;
  lh->value_count = 1;
  lh->shared = 0;
  lh->value_head->value = value;
  lh->value_head->next = NULL;
  return 0;
}

/************************************************************************
 swap_double:
 In:  x, y: doubles to swap
 Out: Swaps references to two double values.
 ***********************************************************************/
void swap_double(double *x, double *y) {
  double temp;

  temp = *x;
  *x = *y;
  *y = temp;
}

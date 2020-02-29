/******************************************************************************
 *
 * Copyright (C) 2006-2017 by
 * The Salk Institute for Biological Studies and
 * Pittsburgh Supercomputing Center, Carnegie Mellon University
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

/**
 * This file is directly included into diffuse_react_event.cpp.
 * The reason why this is not a standard .cpp + .h file is to gove the compiler
 * the opportunity to inline these functions into methods of diffuse&react event.
 */
#include <vector>

#include "logging.h"

#include "diffuse_react_event.h"
#include "defines.h"
#include "world.h"
#include "partition.h"
#include "geometry.h"
#include "debug_config.h"

#include "grid_utils.inc"
#include "wall_utils.inc"
#include "reaction_utils.inc"

using namespace std;

namespace MCell {

namespace DiffusionUtil {


// ---------------------------------- volume mol diffusion ----------------------------------

// get displacement based on scale (related to diffusion constant) and gauss random number
static void pick_vol_displacement(const Species& sp, const float_t scale, rng_state& rng, vec3_t& displacement) {

  if (!sp.can_diffuse()) {
    displacement = vec3_t(0);
  }
  else {
    displacement.x = scale * rng_gauss(&rng) * .70710678118654752440;
    displacement.y = scale * rng_gauss(&rng) * .70710678118654752440;
    displacement.z = scale * rng_gauss(&rng) * .70710678118654752440;
  }
}


// determine how far will our diffused molecule move
static void compute_vol_displacement(
    const Species& sp,
    const float_t max_time,
    rng_state& rng,
    vec3_t& displacement,
    float_t& rate_factor, float_t& r_rate_factor, float_t& steps, float_t& t_steps
) {
  assert(max_time != 0);

  // assert(!m.has_flag(ACT_CLAMPED) && "Not implemented yet");

  t_steps = steps * sp.get_time_step();
  if (t_steps > max_time) {
    t_steps = max_time;
    steps = max_time / sp.get_time_step();
  }
  if (steps < EPS_C) {
    steps = EPS_C;
    t_steps = EPS_C * sp.get_time_step();
  }

  if (steps == 1.0) {
    pick_vol_displacement(sp, sp.get_space_step(), rng, displacement);
    r_rate_factor = rate_factor = 1.0;
  } else {
    rate_factor = sqrt_f(steps);
    r_rate_factor = 1.0 / rate_factor;
    pick_vol_displacement(sp, rate_factor * sp.get_space_step(), rng, displacement);
  }
}

// ---------------------------------- surface mol diffusion ----------------------------------


/*************************************************************************
pick_2D_displacement:
  In: v: vector2 to store the new displacement
      scale: scale factor to apply to the displacement
      rng:
  Out: No return value.  vector is set to a random orientation and a
         distance chosen from the probability distribution of a diffusing
         2D molecule, scaled by the scaling factor.
*************************************************************************/
static void pick_surf_displacement(vec2_t& v, const float_t scale, rng_state& rng) {
  const float_t one_over_2_to_16th = 1.52587890625e-5;
  vec2_t a;

  /*
   * NOTE: The below algorithm is the polar method due to Marsaglia
   * combined with a rejection method for picking uniform random
   * variates in C2.
   * Both methods are nicely described in Chapters V.4.3 and V.4.4
   * of "Non-Uniform Random Variate Generation" by Luc Devroye
   * (http://luc.devroye.org/rnbookindex.html).
   */
  float_t f;
  do {
    unsigned int n = rng_uint(&rng);

    a.u = 2.0 * one_over_2_to_16th * (n & 0xFFFF) - 1.0;
    a.v = 2.0 * one_over_2_to_16th * (n >> 16) - 1.0;
    f = len2_squared(a);
  } while ((f < EPS) || (f > 1.0));

  /*
   * NOTE: The scaling factor to go from a uniform to
   * a normal distribution is sqrt(-2log(f)/f).
   * However, since we use two normally distributed
   * variates to generate a normally distributed
   * 2d vector (with variance 1) we have to normalize
   * and divide by an additional factor of sqrt(2)
   * resulting in normalFactor.
   */
  float_t normal_factor = sqrt_f(-log_f(f) / f);
  v = a * vec2_t(normal_factor * scale);
}


static void compute_surf_displacement(
    const Species& sp,
    const float_t scale,
    rng_state& rng,
    vec2_t& v
) {
  if (sp.can_diffuse()) {
    pick_surf_displacement(v, scale, rng);
  }
  else {
    v = vec2_t(0);
  }
}


/*************************************************************************
move_sm_on_same_triangle:

  This is a helper function for diffuse_2D.

  In: world: simulation state
      sm: molecule that is moving
      new_loc: this is the location we are moving to.
      previous_box: this is the periodic box we were in previously.
      new_wall: this is the new wall we ended up on
      hd_info:
  Out: The grid is created on a new triangle and we place the molecule if
       possible. Counts are updated.
*************************************************************************/
static int move_sm_on_same_triangle(
    Partition& p,
    Molecule& sm,
    vec2_t& new_loc
) {
  Wall& wall = p.get_wall(sm.s.wall_index);
  Grid& grid = wall.grid;
  assert(grid.get_molecule_on_tile(sm.s.grid_tile_index) == sm.id);

  unsigned int new_tile_index = GridUtil::uv2grid_tile_index(new_loc, wall);

  if (new_tile_index >= grid.num_tiles) {
    mcell_internal_error("After ray_trace_2D, selected u, v coordinates "
                         "map to an out-of-bounds grid cell.  uv=(%.2f, "
                         "%.2f) sm=%d/%d",
                         new_loc.u, new_loc.v, new_tile_index, grid.num_tiles);
  }

  // We're on a new part of the grid
  molecule_id_t molecule_id_on_tile = grid.get_molecule_on_tile(new_tile_index);
  if (new_tile_index != sm.s.grid_tile_index) {
    if (molecule_id_on_tile != MOLECULE_ID_INVALID) {
      return 1; /* Pick again--full here */
    }

    grid.reset_molecule_tile(sm.s.grid_tile_index);
    grid.set_molecule_tile(new_tile_index, sm.id);
    sm.s.grid_tile_index = new_tile_index;
  }

  sm.s.pos = new_loc;
  return 0;
}


/*************************************************************************
move_sm_to_new_triangle:

  This is a helper function for diffuse_2D.

  In: world: simulation state
      sm: molecule that is moving
      new_loc: this is the location we are moving to.
      previous_box: this is the periodic box we were in previously.
      new_wall: this is the new wall we ended up on
      hd_info:
  Out: The grid is created on a new triangle and we place the molecule if
       possible. Counts are updated.
*************************************************************************/
static int move_sm_to_new_triangle(
    Partition& p,
    Molecule& sm,
    vec2_t& new_loc,
    const wall_index_t new_wall_index
) {
  Wall& wall = p.get_wall(sm.s.wall_index);
  Grid& grid = wall.grid;
  assert(grid.get_molecule_on_tile(sm.s.grid_tile_index) == sm.id
      && "Mapping grid tile->molecule and molecule->grid tile does not match");

  p.stats.inc_mol_moves_between_walls();

  Wall& new_wall = p.get_wall(new_wall_index);

  // No SM has been here before, so we need to make a grid on this wall.
  if (!new_wall.has_initialized_grid()) {
    new_wall.initialize_grid(p);
  }

  Grid& new_grid = new_wall.grid;

  /* Move to new tile */
  unsigned int new_tile_index = GridUtil::uv2grid_tile_index(new_loc, new_wall);

  if (new_tile_index >= new_grid.num_tiles) {
    mcell_internal_error(
        "After ray_trace_2D to a new wall, selected u, v coordinates map "
        "to an out-of-bounds grid cell.  uv=(%.2f, %.2f) sm=%d/%d",
        new_loc.u, new_loc.v, new_tile_index, new_grid.num_tiles);
  }

  molecule_id_t molecule_id_on_tile = new_grid.get_molecule_on_tile(new_tile_index);
  if (molecule_id_on_tile != MOLECULE_ID_INVALID) {
    return 1; /* Pick again--full here */
  }

  grid.reset_molecule_tile(sm.s.grid_tile_index);
  new_grid.set_molecule_tile(new_tile_index, sm.id);
  sm.s.grid_tile_index = new_tile_index;
  sm.s.wall_index = new_wall_index;

  sm.s.pos = new_loc;

  return 0;
}


static void tiny_diffuse_3D(
    Partition& p,
    Molecule& vm,
    const vec3_t& displacement,
    const wall_index_t previous_reflected_wall,
    vec3_t& new_pos) {

  assert(vm.is_vol());
  assert(vm.v.subpart_index != SUBPART_INDEX_INVALID && "Molecule must be already placed into a subvolume");

  vec3_t ignored_displacement = displacement;
  subpart_index_t new_subpart_index;

  collision_vector_t collisions;

  // NOTE: can be optimized by ignoring molecule collisions
  rng_state ignored_rng;
  ray_trace_vol(
        p, ignored_rng, vm, previous_reflected_wall, ignored_displacement,
        collisions, new_pos, new_subpart_index
  );

  // sort collisions by time
  sort_collisions_by_time(collisions);

  new_pos = vm.v.pos;
  vec3_t new_displacement = displacement;
  for (size_t collision_index = 0; collision_index < collisions.size(); collision_index++) {
    Collision& collision = collisions[collision_index];

    // stop after first collision
    if (collision.is_wall_collision()) {
      new_pos = collision.pos - new_pos;
      new_displacement = displacement * vec3_t(0.5);
    }
  }

  new_pos = new_pos + displacement;
}


static void reflect_absorb_check_wall(
    const Partition& p,
    const Molecule& sm,
    const Wall& wall,
    bool& reflect_now,
    bool& absorb_now
) {
  RxnClassesVector matching_rxns;
  RxUtil::trigger_intersect(
      p, sm, wall,
      matching_rxns
  );

  /* check if this wall has any reflective or absorptive region borders for
   * this molecule (aka special reactions) */
  for (const RxnClass* rxn: matching_rxns) {
    if (rxn->is_reflect()) {
      /* check for REFLECTIVE border */
      reflect_now = true;
      break;
    }
    else if (rxn->is_absorb()) {
      /* check for ABSORPTIVE border */
      absorb_now = 1;
      break;
    }
  }
  /* count hits if we absorb or reflect */
}

/*************************************************************************
reflect_absorb_inside_out:
  In: world: simulation state
      sm: molecule that is moving
      hd_head: region border hit data information
      rx: the type of reaction if any - absorptive/reflective
      matching_rxns: an array of possible reactions
      boundary_pos: the uv coordinates where we hit
      this_wall: the wall that we are on
      index_edge_was_hit: the index of the edge we just hit (0,1,2)
      reflect_now: should the sm reflect
      absorb_now: should the sm be absorbed
      this_wall_edge_region_border:
  Out: 1 if we are about to reflect or absorb (reflect_now, absorb_now). 0
       otherwise. hd_head and this_wall_edge_region_border are updated.
*************************************************************************/
static void reflect_absorb_inside_out(
    const Partition& p,
    const Molecule& sm,
    const Wall& this_wall,
    const edge_index_t edge_index_that_was_hit,
    bool& reflect_now,
    bool& absorb_now
) {
  // missing hit from the second side
  if (WallUtil::is_wall_edge_region_border(p, this_wall, edge_index_that_was_hit, true)) {
    reflect_absorb_check_wall(p, sm, this_wall, reflect_now, absorb_now);
  }
}



/*************************************************************************
reflect_absorb_outside_in:
  In: world: simulation state
      sm: molecule that is moving
      hd_head: region border hit data information
      rx: the type of reaction if any - absorptive/reflective
      matching_rxns: an array of possible reactions
      boundary_pos: the uv coordinates where we hit
      target_wall: the wall we hit
      this_wall: the wall that we are on
      reflect_now: should the sm reflect
      absorb_now: should the sm be absorbed
      this_wall_edge_region_border:
  Out: 1 if we are about to reflect or absorb (reflect_now, absorb_now). 0
       otherwise. hd_head is updated.
*************************************************************************/
static void reflect_absorb_outside_in(
    const Partition& p,
    const Molecule& sm,
    const Wall& target_wall,
    const Wall& this_wall,
    bool& reflect_now,
    bool& absorb_now) {

  /* index of the shared edge in the coordinate system of target wall */
  edge_index_t target_edge_index = WallUtil::find_shared_edge_index_of_neighbor_wall(this_wall, target_wall);

  if (WallUtil::is_wall_edge_region_border(p, target_wall, target_edge_index, true)) {
    reflect_absorb_check_wall(p, sm, target_wall, reflect_now, absorb_now);
  }
}

} // namespace DiffusionUtil

} // namespace mcell

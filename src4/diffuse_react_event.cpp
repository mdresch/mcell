/******************************************************************************
 *
 * Copyright (C) 2019 by
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

#include <iostream>
#include <sstream>
#include <algorithm>
#include <boost/container/flat_set.hpp>

extern "C" {
#include "rng.h"
#include "mcell_structs.h"
#include "logging.h"
}

#include "diffuse_react_event.h"
#include "world.h"
#include "partition.h"
#include "debug_config.h"

typedef boost::container::flat_set<uint32_t> small_subpart_set_t;

using namespace std;

namespace mcell {

void diffuse_react_event_t::step() {
  assert(world->partitions.size() == 1 && "Must extend cache to handle multiple partitions");

  // for each partition
  for (partition_t& p: world->partitions) {
    // diffuse molecules from volume_molecule_indices_per_time_step that have the current diffusion_time_step
    uint32_t time_step_index = p.get_molecule_list_index_for_time_step(diffusion_time_step);
    if (time_step_index != TIME_STEP_INDEX_INVALID) {
      diffuse_molecules(p, p.get_volume_molecule_ids_for_time_step_index(time_step_index));
    }
  }
}


void diffuse_react_event_t::diffuse_molecules(partition_t& p, const std::vector<molecule_id_t>& molecule_ids) {
  float_t event_time_end = event_time + diffusion_time_step;

  // we need to stricly follow the ordering in mcell3, therefore steps 2) and 3) do not use the time
  // for which they were scheduled but rather simply the order in which these "microevents" were created

  // 1) first diffuse already existing molecules
  uint32_t existing_mols_count = molecule_ids.size();
  for (uint32_t i = 0; i < existing_mols_count; i++) {
    molecule_id_t id = molecule_ids[i];
    // existing molecules - simulate whole time step
    diffuse_single_molecule(p, id, diffusion_time_step, event_time_end);
  }

  // 2) we need to take care of unimolecular reactions that were scheduled for this time step
  // in the previous time steps; these unimolecular reaction microevents are handled like they are in a queue

  // first get the calndar owned by partition that contains actions that are scheduled
  uint32_t time_step_index = p.get_or_add_molecule_list_index_for_time_step(diffusion_time_step);
  partition_t::calendar_for_unimol_rxs_t& calendar_for_unimol_rxs =
      p.get_unimolecular_actions_calendar_for_time_step_index(time_step_index);

  // then get bucket for this this->time corresponding to our event time
  uint64_t bucket_index = calendar_for_unimol_rxs.get_bucket_index_for_time(event_time);
  if (bucket_index != BUCKET_INDEX_INVALID) {

    // FIXME: somehow simplify this
    fifo_bucket_t<diffuse_or_unimol_react_action_t>& bucket = calendar_for_unimol_rxs.get_bucket_with_index(bucket_index);
    std::vector<diffuse_or_unimol_react_action_t>& actions = bucket.events;
    for (const diffuse_or_unimol_react_action_t& unimol_action: actions) {
      react_unimol_single_molecule(p, unimol_action.id, unimol_action.scheduled_time, unimol_action.unimol_rx);
    }

    // remove bucket and also all the older ones from out internal scheduler because we processed it
    // FIXME: we can remove multiple items at once
    uint64_t i = 0;
    while (i <= bucket_index) {
      calendar_for_unimol_rxs.pop_bucket();
      i++;
    }
  }

  // 3) simulate remaining time of molecules created with reactions
  // need to call .size() each iteration because the size can increase,
  // again, we are using it as a queue and we do not follow the time when they were created
  for (uint32_t i = 0; i < new_diffuse_or_unimol_react_actions.size(); i++) {
    const diffuse_or_unimol_react_action_t& action = new_diffuse_or_unimol_react_actions[i];

    if (action.type == diffuse_or_unimol_react_action_t::DIFFUSE) {
      diffuse_single_molecule(p, action.id, diffusion_time_step - action.scheduled_time, event_time_end);
    }
    else {
      react_unimol_single_molecule(p, action.id, action.scheduled_time, action.unimol_rx);
    }
  }

  new_diffuse_or_unimol_react_actions.clear();
}


// get displacement based on scale (related to diffusion constant) and gauss random number
static void pick_displacement(float_t scale, rng_state& rng, vec3_t& displacement) {
  displacement.x = scale * rng_gauss(&rng) * .70710678118654752440;
  displacement.y = scale * rng_gauss(&rng) * .70710678118654752440;
  displacement.z = scale * rng_gauss(&rng) * .70710678118654752440;
}


// determine how far will our diffused molecule move
static void compute_displacement(
    species_t& sp,
    rng_state& rng,
    float_t remaining_time_step,
    vec3_t& displacement,
    float_t& r_rate_factor) {

  float_t rate_factor = (remaining_time_step == 1.0) ? 1.0 : sqrt(remaining_time_step);
  r_rate_factor = 1.0 / rate_factor;
  pick_displacement(sp.space_step * rate_factor, rng, displacement);
}


void diffuse_react_event_t::diffuse_single_molecule(
    partition_t& p,
    const molecule_id_t vm_id,
    const float_t time_up_to_event_end,
    const float_t event_time_end
) {

  volume_molecule_t& vm = p.get_vm(vm_id);

  if (vm.is_defunct())
    return;

  // if the molecule is a "newbie", its unimolecular reaction was not yet scheduled
  if ((vm.flags & ACT_NEWBIE) != 0) {
    create_unimol_rx_action(p, vm, time_up_to_event_end);
    vm.flags &= ~ACT_NEWBIE;
  }


#ifdef DEBUG_DIFFUSION
  DUMP_CONDITION4(
    // the subtraction of diffusion_time_step doesn't make much sense but is needed to make the dump the same as in mcell3
    // need to check it further
    vm.dump(world, "", "Diffusing vm:", world->current_iteration, event_time_end - time_up_to_event_end - diffusion_time_step);
  );
#endif

  // we might need to adjust remaining time step if this molecule has a unimolecular reaction
  // within this event's time step range
  float_t remaining_time_step;
  if (vm.unimol_rx_time < event_time_end) { // unlikely
    assert(vm.unimol_rx_time >= event_time && "Missed unimol rx");

    // the value of remaining_time_step passed as argument is time up to event_time_end
    float_t prev_time_from_event_start = diffusion_time_step - time_up_to_event_end;
    float_t new_time_from_event_start = vm.unimol_rx_time - event_time;
    assert(new_time_from_event_start >= prev_time_from_event_start && "Unimol rx cannot be scheduled to the past");

    remaining_time_step = new_time_from_event_start - prev_time_from_event_start;
  }
  else {
    remaining_time_step = time_up_to_event_end;
  }

  species_t& species = world->species[vm.species_id];

  // diffuse each molecule - get information on position change
  // TBD: reflections
  vec3_t displacement;
  float_t r_rate_factor;
  compute_displacement(species, world->rng, remaining_time_step, displacement, r_rate_factor);

#ifdef DEBUG_DIFFUSION
  DUMP_CONDITION4(
    displacement.dump("  displacement:", "");
  );
#endif

  // detect collisions with other molecules
  vec3_t remaining_displacement = displacement;
  uint32_t new_subpart_index;
  vec3_t new_pos;
  ray_trace_state_t state;
  std::vector<molecules_collision_t> molecule_collisions;
  bool was_defunct = false;
  do {
    state =
        ray_trace(
            p, vm /* changes position */,
            remaining_displacement,
            molecule_collisions,
            new_pos,
            new_subpart_index
        );

    // note: this loop always terminates in the first interation for now,
    // prepared for wall collisions
  } while (state != RAY_TRACE_FINISHED);

  // sort collisions by time
  std::sort( molecule_collisions.begin(), molecule_collisions.end(),
      [ ]( const auto& lhs, const auto& rhs )  { return lhs.time < rhs.time;  });

#ifdef DEBUG_COLLISIONS
  DUMP_CONDITION4(
    molecules_collision_t::dump_array(p, molecule_collisions);
  );
#endif

  // evaluate and possible execute reactions
  for (size_t collision_index = 0; collision_index < molecule_collisions.size(); collision_index++) {
    molecules_collision_t& collision = molecule_collisions[collision_index];

    assert(collision.time >= 0 && collision.time <= 1);

    // ignoring immediate collisions
    if (collision.time < EPS) {
        continue;
    }

    // evaluate reaction associated with this collision
    // for now, do the change right away, but we might need to cache these changes and
    // do them after all diffusions were finnished
    // warning: might invalidate references to p.volume_molecules array!
    if (collide_and_react_with_vol_mol(p, collision, displacement, remaining_time_step, r_rate_factor)) {
      // molecule was destroyed
       was_defunct = true;
      break;
    }
  }

  if (!was_defunct) {
    // need to get a new reference
    volume_molecule_t& vm_new_ref = p.get_vm(vm_id);

    // finally move molecule to its destination
    vm_new_ref.pos = new_pos;

    // are we still in the same partition or do we need to move?
    bool move_to_another_partition = !p.in_this_partition(vm_new_ref.pos);
    if (move_to_another_partition) {
      mcell_log("Error: Crossing partitions is not supported yet.\n");
      exit(1);
    }

    // change subpartition
    p.change_molecule_subpartition(vm_new_ref, new_subpart_index);
  }
}


// This function checks if any of the neighboring subpartitions are within radius
// from pos and inserts them into crossed_subparition_indices
static void collect_neighboring_subparts(
    const partition_t& p,
    vec3_t& pos,
    ivec3_t& subpart_indices,
    small_subpart_set_t& crossed_subpart_indices,
    float_t rx_radius,
    float_t subpart_edge_len
) {

  vec3_t rel_pos = pos - p.get_origin_corner();

  // left (x)
  int x_dir_used = 0;
  float_t x_boundary = subpart_indices.x * subpart_edge_len;
  if (rel_pos.x - rx_radius < x_boundary) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x - 1, subpart_indices.y, subpart_indices.z));
    x_dir_used = -1;
  }
  // right (x)
  else if (rel_pos.x + rx_radius > x_boundary + subpart_edge_len) { // assuming that subpartitions are larger than radius
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x + 1, subpart_indices.y, subpart_indices.z));
    x_dir_used = +1;
  }

  // upper (y)
  int y_dir_used = 0;
  float_t y_boundary = subpart_indices.y * subpart_edge_len;
  if (rel_pos.y - rx_radius < y_boundary) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x, subpart_indices.y - 1, subpart_indices.z));
    y_dir_used = -1;
  }
  // right (y)
  else if (rel_pos.y + rx_radius > y_boundary + subpart_edge_len) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x, subpart_indices.y + 1, subpart_indices.z));
    y_dir_used = +1;
  }

  // front (z)
  int z_dir_used = 0;
  float_t z_boundary = subpart_indices.z * subpart_edge_len;
  if (rel_pos.z - rx_radius < z_boundary) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x, subpart_indices.y, subpart_indices.z - 1));
    z_dir_used = -1;
  }
  // back (z)
  else if (rel_pos.z + rx_radius > z_boundary + subpart_edge_len) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x, subpart_indices.y, subpart_indices.z + 1));
    z_dir_used = +1;
  }

  // we also have to count with movement in multiple dimensions
  // xy
  if (x_dir_used != 0 && y_dir_used != 0) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x + x_dir_used, subpart_indices.y + y_dir_used, subpart_indices.z));
  }

  // xz
  if (x_dir_used != 0 && z_dir_used != 0) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x + x_dir_used, subpart_indices.y, subpart_indices.z + z_dir_used));
  }

  // yz
  if (y_dir_used != 0 && z_dir_used != 0) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x, subpart_indices.y + y_dir_used, subpart_indices.z + z_dir_used));
  }

  // xyz
  if (x_dir_used != 0 && y_dir_used != 0 && z_dir_used != 0) {
    crossed_subpart_indices.insert(
        p.get_subpart_index_from_3d_indices(subpart_indices.x + x_dir_used, subpart_indices.y + y_dir_used, subpart_indices.z + z_dir_used));
  }
}


// collect subpartition indices that we are crossing and that are within radius
// of vm that moves by displacement
static void collect_crossed_subparts(
  const partition_t& p,
  volume_molecule_t& vm, // molecule that we are diffusing, we are changing its pos  and possibly also subvolume
  vec3_t& displacement, // in/out - recomputed if there was a reflection
  small_subpart_set_t& crossed_subpart_indices,
  uint32_t& last_subpart_index,
  float_t rx_radius,
  float_t sp_edge_length
) {

  crossed_subpart_indices.clear();
  // remeber the starting subpartition
  crossed_subpart_indices.insert(vm.subpart_index);

  // destination
  vec3_t dest_pos = vm.pos + displacement;

  // urb - upper, right, bottom
  debug_guard_zero_div(displacement);
  ivec3_t dir_urb_direction = ivec3_t(glm::greaterThan(displacement, vec3_t(0)));
  assert(dir_urb_direction.x == 0 || dir_urb_direction.x == 1);
  assert(dir_urb_direction.y == 0 || dir_urb_direction.y == 1);
  assert(dir_urb_direction.z == 0 || dir_urb_direction.z == 1);

  // get 3d indices of start and end subpartitions
  ivec3_t src_subpart_indices, dest_subpart_indices;
  p.get_subpart_3d_indices_from_index(vm.subpart_index, src_subpart_indices);
  p.get_subpart_3d_indices(dest_pos, dest_subpart_indices);

  // first check what's around the starting point
  collect_neighboring_subparts(p, vm.pos, src_subpart_indices, crossed_subpart_indices, rx_radius, sp_edge_length);

  // collect subpartitions on the way by always finding the point where a subpartition boundary is hit
  // we must do it eve when we are crossing just one subpartition because we might hit others while
  // moving along them
  if ( !glm::all( glm::equal(dest_subpart_indices, src_subpart_indices) ) ) {

    uint32_t dest_subpart_index = p.get_subpartition_index_from_3d_indices(dest_subpart_indices);
    last_subpart_index = dest_subpart_index;

    ivec3_t dir_urb_addend;
    dir_urb_addend.x = (dir_urb_direction.x == 0) ? -1 : 1;
    dir_urb_addend.y = (dir_urb_direction.y == 0) ? -1 : 1;
    dir_urb_addend.z = (dir_urb_direction.z == 0) ? -1 : 1;

    vec3_t curr_pos = vm.pos;
    ivec3_t curr_subpart_indices = src_subpart_indices;

    uint32_t curr_sp_index;

    vec3_t displacement_rcp = 1.0/displacement; // POSSIBLE ZERO DIV

    do {
      // subpartition edges
      // = origin + subparition index * length + is_urb * length
      vec3_t sp_len_as_vec3 = vec3_t(sp_edge_length); // FIXME: some of this computation can be moved out of the loop
      vec3_t sp_edges =
          p.get_origin_corner()
          +  vec3_t(curr_subpart_indices) * sp_len_as_vec3 // llf edge
          + vec3_t(dir_urb_direction) * sp_len_as_vec3; // move if we go urb

      // compute time for the next subpartition collision, let's assume that displacemnt
      // is our speed vector and the total time to travel is 1
      //
      // pos(time) = pos + displacement * time, therefore
      // time = (pos(time) - vm.pos) / displacement
      // =>
      // time_to_subpart_edge = (subpart_edge - vm.pos) / displacement_speed
      vec3_t coll_times = (sp_edges - curr_pos) * displacement_rcp;
      assert(coll_times.x >= 0 && coll_times.y >= 0 && coll_times.z >= 0 && "Edges must be computed from direction");

      // which of the times is the smallest? - i.e. which boundary we hit first
      if (coll_times.x >= 0 && coll_times.x < coll_times.y && coll_times.x <= coll_times.z) {
        // new position on the edge of the subpartition
        curr_pos += displacement * coll_times.x;
        // and also update the xyz subpartition index
        curr_subpart_indices.x += dir_urb_addend.x;
      }
      else if (coll_times.y >= 0 && coll_times.y <= coll_times.z) {
        curr_pos += displacement * coll_times.y;
        curr_subpart_indices.y += dir_urb_addend.y;
      }
      else if (coll_times.z >= 0) {
        curr_pos += displacement * coll_times.z;
        curr_subpart_indices.z += dir_urb_addend.z;
      }
      else {
        break;
      }

      curr_sp_index = p.get_subpartition_index_from_3d_indices(curr_subpart_indices);
      crossed_subpart_indices.insert(curr_sp_index);

      // also neighbors
      collect_neighboring_subparts(p, curr_pos, curr_subpart_indices, crossed_subpart_indices, rx_radius, sp_edge_length);

    } while (curr_sp_index != dest_subpart_index);
  }
  else {
    // subpartition index did not change
    last_subpart_index = vm.subpart_index;
  }

  // finally check also neighbors in destination
  collect_neighboring_subparts(p, dest_pos, dest_subpart_indices, crossed_subpart_indices, rx_radius, sp_edge_length);
}


// check whether diffused_vm molecule collision that moves by displacement can collide
// with colliding_vm; returns true if there can be a collision and returns relative collision
// time and relative position
static bool collide_mol(
    volume_molecule_t& diffused_vm,
    vec3_t& displacement,
    volume_molecule_t& colliding_vm,
    float_t& rel_collision_time,
    vec3_t& rel_collision_pos,
    float_t rx_radius_3d) {

  vec3_t& pos = colliding_vm.pos; /* Position of target molecule */
  vec3_t dir = pos - diffused_vm.pos;  /* From starting point of moving molecule to target */

  float_t d = glm::dot((glm_vec3_t)dir, (glm_vec3_t)displacement);        /* Dot product of movement vector and vector to target */

  /* Miss the molecule if it's behind us */
  if (d < 0) {
    return false;
  }

  float_t movelen2 = glm::dot((glm_vec3_t)displacement, (glm_vec3_t)displacement); /* Square of distance the moving molecule travels */

  /* check whether the test molecule is further than the displacement. */
  if (d > movelen2) {
    return false;
  }

  /* check whether the moving molecule will miss interaction disk of the
     test molecule.*/
  float_t dirlen2 = glm::dot((glm_vec3_t)dir, (glm_vec3_t)dir);
  float_t sigma2 = rx_radius_3d * rx_radius_3d;   /* Square of interaction radius */
  if (movelen2 * dirlen2 - d * d > movelen2 * sigma2) {
    return false;
  }

  /* reject collisions with itself */
  if (diffused_vm.id == colliding_vm.id) {
    return false;
  }

  /* defunct - not probable */
  if (colliding_vm.is_defunct()) {
    return false;
  }

  rel_collision_time = d / movelen2;

  rel_collision_pos = diffused_vm.pos + rel_collision_time * displacement;
  return COLLIDE_VOL_M;
}


// body of the collision detection loop
static void ray_trace_loop_body(
    partition_t& p,
    volume_molecule_t& vm,
    molecule_id_t colliding_vm_id,
    vec3_t& remaining_displacement,
    std::vector<molecules_collision_t>& molecule_collisions,
    world_t* world,
    float_t radius
    ) {

  volume_molecule_t& colliding_vm = p.get_vm(colliding_vm_id);

  // we would like to compute everything that's needed just once
  float_t time;
  vec3_t position;
  // collide_mol must be inlined because many things are computed all over there
  if (collide_mol(vm, remaining_displacement, colliding_vm, time, position, radius)) {
    reaction_t* rx = world->get_reaction(vm, colliding_vm);
    assert(rx != nullptr);
    molecule_collisions.push_back(
        molecules_collision_t(&p, vm.id, colliding_vm.id, rx, time, position)
    );
  }
}


// collect possible collisions for molecule vm that has to displace by remaining_displacement,
// returns possible collisions in molecule_collisions, new position in new_pos and
// index of the new subparition in new_subpart_index
// later, this will check collisions until a wall is hit
ray_trace_state_t diffuse_react_event_t::ray_trace(
    partition_t& p,
    volume_molecule_t& vm, // molecule that we are diffusing, we are changing its pos  and possibly also subvolume
    vec3_t& remaining_displacement, // in/out - recomputed if there was a reflection
    std::vector<molecules_collision_t>& molecule_collisions,
    vec3_t& new_pos,
    uint32_t& new_subpart_index
    ) {

  // first get what subpartitions might be relevant
  small_subpart_set_t crossed_subparition_indices;
  uint32_t last_subpartition_index;
  collect_crossed_subparts(
      p, vm, remaining_displacement,
      crossed_subparition_indices, last_subpartition_index,
      world->world_constants.rx_radius_3d,
      world->world_constants.subpartition_edge_length
  );

  float_t radius = world->world_constants.rx_radius_3d;

  // TBD: check wall collisions
  // here we can return RAY_TRACE_HIT_WALL

  // for each SP
  for (uint32_t subpart_index: crossed_subparition_indices) {
    // get cached reacting molecules for this SP
    subpartition_mask_t& sp_reactants = p.get_volume_molecule_reactants(subpart_index, vm.species_id);

    // for each molecule in this SP
    for (uint32_t colliding_vm_index: sp_reactants) {
      ray_trace_loop_body(
          p,
          vm,
          colliding_vm_index,
          remaining_displacement,
          molecule_collisions,
          world,
          radius
      );
    }
  }

  // the value is valid only when RAY_TRACE_FINISHED is returned
  new_subpart_index = last_subpartition_index;
  new_pos = vm.pos + remaining_displacement;

  return RAY_TRACE_FINISHED; // no wall was hit
}


// handle collision of two volume molecules: checks probability of reaction,
// executes this reaction, removes reactants and creates products
// returns true if reaction has occured and the first reactant was destroyed
bool diffuse_react_event_t::collide_and_react_with_vol_mol(
    partition_t& p,
    molecules_collision_t& collision,
    vec3_t& displacement,
    float_t remaining_time_step,
    float_t r_rate_factor
)  {

  volume_molecule_t& colliding_molecule = p.get_vm(collision.colliding_molecule_idx); // am
  volume_molecule_t& diffused_molecule = p.get_vm(collision.diffused_molecule_idx); // m

  // returns 1 when there are no walls at all
  //TBD: double factor = exact_disk(

  reaction_t& rx = *collision.rx;
  //  rx->prob_t is always NULL in out case update_probs(world, rx, m->t);
  // returns which reaction pathway to take
  float_t scaling = /*factor - 1.0*/ r_rate_factor;
  int i = test_bimolecular(
    rx, colliding_molecule, diffused_molecule, scaling);

  if (i < RX_LEAST_VALID_PATHWAY) {
    return false;
  }
  else {
    // might invalidate references
    int j = outcome_bimolecular(p, collision, i, remaining_time_step);
    assert(j == RX_DESTROY);
    return true;
  }
}


/*************************************************************************
test_bimolecular
  In: the reaction we're testing
      a scaling coefficient depending on how many timesteps we've
        moved at once (1.0 means one timestep) and/or missing interaction area
      local probability factor (positive only for the reaction between two
        surface molecules, otherwise equal to zero)
      reaction partners
  Out: RX_NO_RX if no reaction occurs
       int containing which reaction pathway to take if one does occur
  Note: If this reaction does not return RX_NO_RX, then we update
        counters appropriately assuming that the reaction does take place.
*************************************************************************/
/*, double scaling - 1, double local_prob_factor = 0,*/
int diffuse_react_event_t::test_bimolecular(
    reaction_t& rx,
    volume_molecule_t& a1,
    volume_molecule_t& a2,
    float_t scaling
) {
  /* rescale probabilities for the case of the reaction
     between two surface molecules */
  float_t min_noreaction_p = rx.min_noreaction_p; // local_prob_factor == 0

  assert(min_noreaction_p < scaling);
  /* Instead of scaling rx->cum_probs array we scale random probability */
  float_t p = rng_dbl(&(world->rng)) * scaling;

  if (p >= min_noreaction_p) {
    return RX_NO_RX;
  }
  else {
    return 0; // we have just one pathwayy
  }
}


// checks if reaction should probabilistically occur and if so,
// destroys reactants
// returns RX_DESTROY when reactants were destroyed
int diffuse_react_event_t::outcome_bimolecular(
    partition_t& p,
    molecules_collision_t& collision,
    int path,
    float_t remaining_time_step
) {

  // might invalidate references!
  int result =
      outcome_products_random(
        p,
        collision.rx,
        collision.pos,
        collision.time,
        remaining_time_step,
        path
      );

  if (result == RX_A_OK) {
    volume_molecule_t& reacA = p.get_vm(collision.diffused_molecule_idx);
    volume_molecule_t& reacB = p.get_vm(collision.colliding_molecule_idx);

#ifdef DEBUG_REACTIONS
    // reference printout first destroys B then A
    DUMP_CONDITION4(
      reacB.dump(world, "", "  defunct vm:", world->current_iteration);
      reacA.dump(world, "", "  defunct vm:", world->current_iteration);
    );
#endif

    // always for now
    // we used the reactants - remove them
    p.set_molecule_as_defunct(reacA);
    p.set_molecule_as_defunct(reacB);

    return RX_DESTROY;
  }


  return result;
}


// why is this called "random"? - check if reaction occurs is in test_bimolecular
// mcell3 version returns  cross_wall ? RX_FLIP : RX_A_OK;
// ! might invalidate references
int diffuse_react_event_t::outcome_products_random(
    partition_t& p,
    const reaction_t* rx,
    const vec3_t& pos,
    float_t reaction_time,
    float_t remaining_time_step,
    int path
) {
  assert(path == 0 && "Only single pathway is supported now");
  // we can have just one product for now and no walls

  // create and place each product
  for (const species_with_orientation_t& product: rx->products) {
    volume_molecule_t vm(MOLECULE_ID_INVALID, product.species_id, pos);

    volume_molecule_t& new_vm = p.add_volume_molecule(vm, world->species[vm.species_id].time_step);
    new_vm.flags =  ACT_NEWBIE | TYPE_VOL | IN_VOLUME | ACT_DIFFUSE;

  #ifdef DEBUG_REACTIONS
    DUMP_CONDITION4(
      new_vm.dump(world, "", "  created vm:", world->current_iteration);
    );
  #endif

    float_t scheduled_time;
    if (rx->reactants.size() == 2) {
      // bimolecular reaction
      // schedule new product for diffusion
      // collision.time is relative to the part that this molecule travels this diffusion step
      // so it needs to be scaled
      scheduled_time = diffusion_time_step - (remaining_time_step - reaction_time*remaining_time_step);
    }
    else {
      // unimolecular reaction
      // reaction_time is the time when this new molecule was created
      scheduled_time = reaction_time;
    }

    // NOTE: in this time step, we will simply simulate all results of reactions regardless on the diffusion time step of the
    // particular product
    // we alway create diffuse events, unimol react events are created elsewhere
    new_diffuse_or_unimol_react_actions.push_back(
        diffuse_or_unimol_react_action_t(new_vm.id, scheduled_time, diffuse_or_unimol_react_action_t::DIFFUSE));
  }
  return RX_A_OK;
}

// ---------------------------------- unimolecular reactions ----------------------------------

// might return nullptr if there is no unimolecular reaction for this species
// based on pick_unimolecular_reaction
static const reaction_t* pick_unimol_rx(
    const world_t* world,
    const species_id_t species_id
) {
  const unimolecular_reactions_map_t* unimol_rxs = world->world_constants.unimolecular_reactions_map;
  auto it = unimol_rxs->find(species_id);
  if (it == unimol_rxs->end()) {
    return nullptr;
  }
  else {
    return it->second;
  }
}


// based on timeof_unimolecular
static float_t time_of_unimol(const reaction_t* rx, rng_state& rng) {
  double k_tot = rx->max_fixed_p;
  double p = rng_dbl(&rng);

  if ((k_tot <= 0) || (!distinguishable(p, 0, EPS)))
    return FOREVER;
  return -log(p) / k_tot;
}


// based on compute_lifetime
static float_t compute_unimol_lifetime(
    world_t* world,
    const volume_molecule_t& vm,
    const reaction_t* rx
) {
  assert(rx != nullptr);

  float_t res = time_of_unimol(rx, world->rng);

#ifdef DEBUG_REACTIONS
  DUMP_CONDITION4(
      // calling rng for unimolecular
      vm.dump(world, "Assigned unimolecular time (prev rng):", "", world->current_iteration, res);
  );
#endif

  return res;
}


void diffuse_react_event_t::create_unimol_rx_action(
    partition_t& p,
    volume_molecule_t& vm,
    float_t remaining_time_step
) {
  float_t curr_time = event_time + diffusion_time_step - remaining_time_step;
  assert(curr_time >= 0);

  const reaction_t* rx = pick_unimol_rx(world, vm.species_id);
  if (rx == nullptr) {
    return;
  }

  float_t time_from_now = compute_unimol_lifetime(world, vm, rx);

  float_t scheduled_time = curr_time + time_from_now;

  // we need to store the end time to the molecule because oit is needed in diffusion to
  // figure out whether we should do the whole time step
  vm.unimol_rx_time = scheduled_time;

  // now, there are two queues - local for this timestep
  // and global in partition for the following timesteps7
  diffuse_or_unimol_react_action_t unimol_react_action(
      vm.id, scheduled_time, diffuse_or_unimol_react_action_t::UNIMOL_REACT, rx);

  if (scheduled_time < event_time + diffusion_time_step) {
    // handle this iteration
    new_diffuse_or_unimol_react_actions.push_back(unimol_react_action);
  }
  else {
    p.add_unimolecular_action(diffusion_time_step, unimol_react_action);
  }
}


int diffuse_react_event_t::outcome_unimolecular(
    partition_t& p,
    volume_molecule_t& vm,
    const float_t time_from_event_start,
    const reaction_t* unimol_rx
) {
  molecule_id_t id = vm.id;

  // creates new molecule(s) as output of the unimolecular reaction
  // !! might invalidate references (we might reorder defuncting and outcome call later)
  int outcome_res = outcome_products_random(p, unimol_rx, vm.pos, time_from_event_start, TIME_INVALID, 0);
  assert(outcome_res == RX_A_OK);

  // and defunct this molecule
  volume_molecule_t& vm_new_ref = p.get_vm(id);
#ifdef DEBUG_REACTIONS
  DUMP_CONDITION4(
    vm_new_ref.dump(world, "", "Unimolecular vm defunct:", world->current_iteration, time_from_event_start);
  );
#endif
  p.set_molecule_as_defunct(vm_new_ref);
  return RX_DESTROY;
}


// based on mcell3's check_for_unimolecular_reaction
// might invalidate vm references
void diffuse_react_event_t::react_unimol_single_molecule(
    partition_t& p,
    const molecule_id_t vm_id,
    const float_t scheduled_time,
    const reaction_t* unimol_rx
) {
  assert(unimol_rx != nullptr);
  // the unimolecular reaction was already selected
  // FIXME: if there is more of them, mcell3 uses rng to select which to execute...
  volume_molecule_t& vm = p.get_vm(vm_id);
  if (vm.is_defunct()) {
    return;
  }

  assert(scheduled_time >= event_time && scheduled_time <= event_time + diffusion_time_step);
  int rx_res = outcome_unimolecular(p, vm, scheduled_time - event_time, unimol_rx);
  assert(rx_res == RX_DESTROY);
}


// ---------------------------------- dumping methods ----------------------------------

void diffuse_react_event_t::dump(const std::string indent) {
  cout << indent << "Diffuse-react event:\n";
  std::string ind2 = indent + "  ";
  base_event_t::dump(ind2);
  cout << ind2 << "diffusion_time_step: \t\t" << diffusion_time_step << " [float_t] \t\t\n";
}


void molecules_collision_t::dump(partition_t& p, const std::string ind) const {
  cout << ind << "diffused_molecule:\n";
  p.get_vm(diffused_molecule_idx).dump(ind + "  ");
  cout << ind << "colliding_molecule:\n";
  p.get_vm(colliding_molecule_idx).dump(ind + "  ");
  cout << ind << "reaction:";
  rx->dump(ind + "  ");

  cout << "time: \t\t" << time << " [float_t] \t\t\n";
  cout << "position: \t\t" << pos << " [vec3_t] \t\t\n";
}


string molecules_collision_t::to_string() const {
  stringstream ss;
  ss
    //  << "diff_idx: " << diffused_molecule_idx
      << "coll_idx: " << colliding_molecule_idx
      << ", time: " << time
      << ", pos: " << pos;
  return ss.str();
}


void molecules_collision_t::dump_array(partition_t& p, const std::vector<molecules_collision_t>& vec) {
  // printed in resverse - same as
  for (size_t i = 0; i < vec.size(); i++) {
    cout << "  " << "collision " << i << ": " << vec[i].to_string() << "\n";
  }
}

} /* namespace mcell */

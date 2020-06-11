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

#include "defines.h"

#include <iostream>
#include <sstream>

#ifdef DWITHGPERFTOOLS
// using longer path to avoid collisions
#include "install_gperftools/include/profiler.h"
#endif

using namespace std;

namespace MCell {

string Vec3::to_string() const {
  stringstream ss;
  ss << *this;
  return ss.str();
}


void Vec3::dump(const std::string extra_comment, const std::string ind) const {
  cout << ind << extra_comment << *this << "\n";
}


string Vec2::to_string() const {
  stringstream ss;
  ss << *this;
  return ss.str();
}


void Vec2::dump(const std::string extra_comment, const std::string ind) const {
  cout << ind << extra_comment << *this << "\n";
}


void SimulationStats::dump() {
  cout << "Total number of ray-subvolume intersection tests (number of ray_trace calls): " << ray_voxel_tests << "\n";
  cout << "Total number of ray-polygon intersection tests: " << ray_polygon_tests << "\n";
  cout << "Total number of ray-polygon intersections: " << ray_polygon_colls << "\n";
  cout << "Total number of molecule moves between walls: " << mol_moves_between_walls << "\n";
  cout << "Total number of usages of waypoints for counted volumes: " << num_waypoints_used << "\n";
  cout << "Total number of counted volume recomputations: " << recomputations_of_counted_volume << "\n";
}


void SimulationConfig::dump() {
  BNGConfig::dump();
  cout << "SimulationConfig:\n";
  cout << "  vacancy_search_dist2: \t\t" << vacancy_search_dist2 << " [float_t] \t\t\n";
  cout << "  partition_edge_length: \t\t" << partition_edge_length << " [float_t] \t\t\n";
  cout << "  num_subpartitions_per_partition: \t\t" << num_subpartitions_per_partition << " [uint] \t\t\n";
  cout << "  num_subpartitions_per_partition_squared: \t\t" << num_subpartitions_per_partition_squared << " [uint] \t\t\n";
  cout << "  subpartition_edge_length: \t\t" << subpartition_edge_length << " [float_t] \t\t\n";
  cout << "  subpartition_edge_length_rcp: \t\t" << subpartition_edge_length_rcp << " [float_t] \t\t\n";
  cout << "  use_expanded_list: \t\t" << use_expanded_list << " [bool] \t\t\n";
  cout << "  randomize_smol_pos: \t\t" << randomize_smol_pos << " [bool] \t\t\n";
}

} // namespace mcell

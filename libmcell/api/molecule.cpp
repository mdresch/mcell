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

#include "molecule.h"

#include "world.h"
#include "partition.h"

namespace MCell {
namespace API {

void Molecule::remove() {
  check_initialization();

  Partition& p = world->get_partition(PARTITION_ID_INITIAL);

  if (!p.does_molecule_exist(id)) {
    throw RuntimeError("Molecule with id " + std::to_string(id) + " does not exist anymore.");
  }

  // set that this molecule is defunct
  world->get_partition(PARTITION_ID_INITIAL).get_m(id).set_is_defunct();
}

}
}

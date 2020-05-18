/******************************************************************************
 *
 * Copyright (C) 2020 by
 * The Salk Institute for Biological Studies and
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

#include "model.h"
#include "world.h"
#include "mcell4_converter.h"

namespace MCell {
namespace API {

Model::~Model() {
  delete world;
}


void Model::initialize() {
  if (world != nullptr) {
    throw RuntimeError("Model.initialize can be called only once");
  }

  world = new World();

  // semantic checks are done during conversion
  MCell4Converter converter;

  converter.convert(this, world);
}


void Model::dump() const {
  // TODO
  // std::cout << to_str() << "\n";
}

}
}


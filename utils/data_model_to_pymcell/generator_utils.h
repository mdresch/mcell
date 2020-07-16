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

/**
 * This file is directly included because it contains templates and also to avoid the
 * need to have two declarations (in .h + in .cpp) for each function.
 */

#ifndef SRC4_PYMCELLCONVERTER_GENERATOR_UTILS_H_
#define SRC4_PYMCELLCONVERTER_GENERATOR_UTILS_H_

#include <exception>
#include <iostream>
#include <string>
#include <cassert>

#include "libmcell/generated/gen_names.h"
#include "include/datamodel_defines.h"

#include "generator_constants.h"

using namespace std;

namespace MCell {

using Json::Value;

typedef invalid_argument ConversionError;

// using exceptions to

#define CHECK(stmt, failed) \
  do { \
    try { \
      (stmt); \
    } \
    catch (exception& e) { \
      cerr << e.what() << "\n"; \
      cerr << "Exception caught in '" << __FUNCTION__ << "' after conversion error.\n"; \
      failed = true; \
    } \
  } while (0)

#define CHECK_PROPERTY(cond) \
  do { \
    if(!(cond)) { \
      throw ConversionError(S("Expected '") + #cond + "' is false. (" + __FUNCTION__ + " - " + __FILE__ + ":" + to_string(__LINE__) + ")"); \
    } \
  } while (0)

#define ERROR(msg) throw ConversionError(msg)


// auxiliary method to simply convert to std::string for when concatenating string
static string S(const char* s) {
  return string(s);
}


// throws exception when the member is member is there
static Value& get_node(const string parent_name, Value& parent, const string name) {
  if (!parent.isMember(name)) {
    throw ConversionError("Node '" + parent_name + "' does not contain expected node '" + name + "'.");
  }
  return parent[name];
}


// used when we know that the member is there
static Value& get_node(Value& parent, const string name) {
  assert(parent.isMember(name));
  return parent[name];
}


static void gen_comma(ostream& out, Value::ArrayIndex index, Value& array) {
  if (index + 1 != array.size()) {
    out << ", ";
  }
}


template<typename T>
static void gen_comma(ostream& out, size_t index, const vector<T>& array) {
  if (index + 1 != array.size()) {
    out << ", ";
  }
}


template<typename T>
void print_comma(ostream& out, size_t index, const vector<T>& array) {
  if (index + 1 != array.size()) {
    out << ", ";
  }
}


static string make_section_comment(const string text) {
  return BLOCK_BEGIN1 + text + BLOCK_BEGIN2 + "\n";
}


static string make_start_block_comment(const string text) {
  return BLOCK_BEGIN1 + text + BLOCK_BEGIN2;
}


static string make_end_block_comment(const string text) {
  return BLOCK_END1 + text + BLOCK_END2 + "\n";
}


static string make_enum_value(const string enum_name, const string value) {
  return MDOT + enum_name + "." + value;
}


static void check_versions(
    const string node_name, Json::Value& node,
    const char* const version1, const char* const version2) {
  if (node[KEY_DATA_MODEL_VERSION].asString() != version1 &&
      node[KEY_DATA_MODEL_VERSION].asString() != version2) {
    throw ConversionError(
        "Error: version for " + node_name + " is " + node[KEY_DATA_MODEL_VERSION].asString() +
        ", expected " + version1 + " or " + version2 + ".");
  }
}


static void check_version(const string node_name, Json::Value& node, const char* const version) {
  if (node[KEY_DATA_MODEL_VERSION].asString() != version) {
    throw ConversionError(
        "Error: version for " + node_name + " is " + node[KEY_DATA_MODEL_VERSION].asString() +
        ", expected " + version + ".");
  }
}


// replaces '.' with '_' and does potentially other conversions
string make_id(const string& s) {
  string res = s;
  // do not do changes if the ID starts with 'm.' -> constant from
  // the mcell module ID that cannot have dots that we need to replace in it anyway
  if (res.size() <= 2 || (res.size() > 2 && res.substr(0, strlen(MDOT)) != MDOT)) {
    replace(res.begin(), res.end(), '.', '_');
  }
  return res;
}

// name might be empty
void gen_ctor_call(ofstream& out, string name, string class_name, bool has_params = true) {
  if (name != "") {
    out << make_id(name) << " = " << MDOT << class_name;
  }
  else {
    out << MDOT << class_name;
  }
  if (has_params) {
    out << "(\n";
  }
  else {
    out << "()\n";
  }
}


void gen_method_call(ofstream& out, string obj, string method, string param = "") {
  out << obj << "." << method << "(" << param << ")\n";
}


template<typename T>
void gen_param(ofstream& out, string name, T value, bool comma) {
  out << IND << name << " = " << value << (comma?",":"") << "\n";
}


template<>
void gen_param(ofstream& out, string name, Json::Value& value, bool comma) {
  out << IND << name << " = '" << value.asString() << "'" << (comma?",":"") << "\n";
}


template<>
void gen_param(ofstream& out, string name, string value, bool comma) {
  out << IND << name << " = '" << value << "'" << (comma?",":"") << "\n";
}


template<>
void gen_param(ofstream& out, string name, const char* const value, bool comma) {
  out << IND << name << " = '" << value << "'" << (comma?",":"") << "\n";
}


template<>
void gen_param(ofstream& out, string name, int value, bool comma) {
  out << IND << name << " = " << value << (comma?",":"") << "\n";
}


template<>
void gen_param(ofstream& out, string name, double value, bool comma) {
  out << IND << name << " = " << value << (comma?",":"") << "\n";
}

template<>
void gen_param(ofstream& out, string name, bool value, bool comma) {
  out << IND << name << " = " << (value ? "True" : "False") << (comma?",":"") << "\n";
}

void gen_param_id(ofstream& out, string name, string id, bool comma) {
  out << IND << name << " = " << make_id(id) << (comma?",":"") << "\n";
}


void gen_param_id(ofstream& out, string name, Json::Value& id, bool comma) {
  out << IND << name << " = " << make_id(id.asString()) << (comma?",":"") << "\n";
}


void gen_param_expr(ofstream& out, string name, const string& value, bool comma) {
  string python_expr;
  // replace operator ^ with operator **
  python_expr = regex_replace(value, regex("\\^"), "**");
  out << IND << name << " = " << value << (comma?",":"") << "\n";
}

// this should be used when printing out floating point values (doubles)
void gen_param_expr(ofstream& out, string name, Json::Value& value, bool comma) {
  gen_param_expr(out, name, value.asString(), comma);
}


void gen_param_enum(ofstream& out, string name, string enum_name, string enum_value, bool comma) {
  out << IND << name << " = " << make_enum_value(enum_name, enum_value) << (comma?",":"") << "\n";
}

void gen_param_list(ofstream& out, string name, const vector<string>& values, bool comma) {
  out << IND << name << " = [";
  for (size_t i = 0; i < values.size(); i++) {
    out << values[i];
    gen_comma(out, i , values);
  }

  out << "]" << (comma?",":"") << "\n";
}

void gen_param_vec3(ofstream& out, string name, Json::Value& x, Json::Value& y, Json::Value& z, bool comma) {
  out << IND <<
      name << " = " << MDOT << VEC3 <<
      "(" << x.asString() << ", " << y.asString() << ", " << z.asString() << ")" << (comma?",":"") << "\n";
}


template<typename T>
void gen_assign(ofstream& out, string obj_name, string field_name1, string field_name2, T value) {
  out << obj_name << "." << field_name1 << "." << field_name2 << " = " << value << "\n";
}


// for some reason the template above casts double to int..
template<>
void gen_assign(ofstream& out, string obj_name, string field_name1, string field_name2, double value) {
  out << obj_name << "." << field_name1 << "." << field_name2 << " = " << value << "\n";
}


template<>
void gen_assign(ofstream& out, string obj_name, string field_name1, string field_name2, float value) {
  out << obj_name << "." << field_name1 << "." << field_name2 << " = " << value << "\n";
}


template<>
void gen_assign(ofstream& out, string obj_name, string field_name1, string field_name2, bool value) {
  out << obj_name << "." << field_name1 << "." << field_name2 << " = " << (value ? "True" : "False") << "\n";
}


static string convert_orientation(const string s, const bool return_any_orientation = false) {
  if (s == "\'") {
    return API::NAME_EV_UP;
  }
  else if (s == ",") {
    return API::NAME_EV_DOWN;
  }
  else if (s == ";" || s == "") {
    if (return_any_orientation && s == ";") {
      return API::NAME_EV_ANY;
    }
    else {
      return "";
    }
  }
  else {
    ERROR("Invalid orientation '" + s + "'.");
    return "INVALID_ORIENTATION";
  }
}


static void gen_rxn_substance_inst(ofstream& out, Json::Value& substances_node) {
  string str = substances_node.asString();

  // special case for rxns without products
  if (str == NULL_PRODUCTS) {
    out << "[ ]";
    return;
  }

  vector<string> substances;
  vector<string> orientations;

  // finite automata to parse the reaction side string, e.g. "a + b"
  enum state_t {
    START,
    ID,
    AFTER_ID,
    AFTER_ORIENT,
    AFTER_PLUS
  };

  state_t state = START;
  string current_id;
  for (size_t i = 0; i < str.size(); i++) {
    char c = str[i];
    switch (state) {
      case START:
        if (isalnum(c) || c == '_') {
          state = ID;
          current_id = c;
        }
        else if (c == '.') {
          state = ID;
          current_id = '_';
        }
        else if (isblank(c)) {
          // ok
        }
        else {
          ERROR("Could not parse reaction side " + str + " (START).");
        }
        break;

      case ID:
        if (isalnum(c) || c == '_') {
          current_id += c;
        }
        else if (c == '.') {
          state = ID;
          current_id += '_';
        }
        else if (isblank(c) || c == '+' || c == '\'' || c == ',' || c == ';') {
          substances.push_back(current_id);
          orientations.push_back("");
          if (c == '\'' || c == ',' || c == ';') {
            orientations.back() = c;
          }
          current_id = "";
          if (c == '+') {
            state = AFTER_PLUS;
          }
          else {
            state = AFTER_ID;
          }
        }
        else {
          ERROR("Could not parse reaction side " + str + " (ID).");
        }
        break;

      case AFTER_ID:
        if (c == '+') {
          state = AFTER_PLUS;
        }
        else if (c == '\'') {
          state = AFTER_ORIENT;
          orientations.back() = c;
        }
        else if (c == ',') {
          state = AFTER_ORIENT;
          orientations.back() = c;
        }
        else if (c == ';') {
          state = AFTER_ORIENT;
          orientations.back() = c;
        }
        else if (isblank(c)) {
          // ok
        }
        else {
          ERROR("Could not parse reaction side " + str + " (AFTER_ID).");
        }
        break;

      case AFTER_ORIENT:
        if (c == '+') {
          state = AFTER_PLUS;
        }
        else if (isblank(c)) {
          // ok
        }
        else {
          ERROR("Could not parse reaction side " + str + " (AFTER_ID).");
        }
        break;

      case AFTER_PLUS:
        if (isalnum(c) || c == '_') {
          state = ID;
          current_id = c;
        }
        else if (c == '.') {
          state = ID;
          current_id = '_';
        }
        else if (isblank(c)) {
          // ok
        }
        else {
          ERROR("Could not parse reaction side " + str + " (AFTER_PLUS).");
        }
        break;
      default:
        assert(false);
    }
  }
  if (current_id != "") {
    substances.push_back(current_id);
    orientations.push_back("");
  }

  out << "[ ";
  for (size_t i = 0; i < substances.size(); i++) {
    out << substances[i] << "." << API::NAME_INST << "(";

    string orient = convert_orientation(orientations[i], true);
    if (orient != "") {
      out << API::NAME_ORIENTATION << " = " << MDOT << API::NAME_ENUM_ORIENTATION << "." << orient;
    }

    out << ")";
    print_comma(out, i, substances);
  }
  out << " ]";
}


bool convert_reaction_name(const string& json_name, string& res_name) {
  res_name = json_name;
  replace(res_name.begin(), res_name.end(), ' ', '_');
  replace(res_name.begin(), res_name.end(), '.', '_');
  replace(res_name.begin(), res_name.end(), ')', '_');
  replace(res_name.begin(), res_name.end(), '(', '_');

  res_name = regex_replace(res_name, regex("<->"), "to");
  res_name = regex_replace(res_name, regex("->"), "to");
  res_name = regex_replace(res_name, regex("\\+"), "plus");
  res_name = regex_replace(res_name, regex("'"), "_up");
  res_name = regex_replace(res_name, regex(","), "_down");
  res_name = regex_replace(res_name, regex(";"), "_any");

  // TODO: check for invalid cases
  // return false in that case

  return true; // ok
}


// NOTE: the same code is in mcell3_world_converter.cpp
static bool ends_with(std::string const & value, std::string const & ending)
{
    if (ending.size() > value.size()) {
      return false;
    }
    return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}


string trim(const string& str)
{
    size_t first = str.find_first_not_of(' ');
    if (string::npos == first)
    {
        return str;
    }
    size_t last = str.find_last_not_of(' ');
    return str.substr(first, (last - first + 1));
}

} // namespace MCell

#endif // SRC4_PYMCELLCONVERTER_GENERATOR_UTILS_H_

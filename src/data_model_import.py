import pymcell as m
import json
from typing import List, Dict


def read_json_data_model(file_name: str):
    with open(file_name, 'r') as f:
        json_model = f.read()
        data_model = json.loads(json_model)
    return data_model


def create_species_from_dm(data_model: Dict) -> List[m.Species]:
    species_dm_list = data_model['mcell']['define_molecules']['molecule_list']
    species_dict = {}
    for species_dm in species_dm_list:
        species_name = species_dm['mol_name']
        dc = float(species_dm['diffusion_constant'])
        species_type = species_dm['mol_type']
        surface = False
        if species_type == '2D':
            surface = True
        species_dm = m.Species(species_name, dc, surface)
        species_dict[species_name] = species_dm
    return species_dict


def make_spec_orient_list(mol_str_list, species):
    spec_orient_list = []
    for r in mol_str_list:
        if r.endswith("'") or r.endswith(",") or r.endswith(";"):
            r_str = r[:-1]
            r_orient = r[-1]
            if r_orient == "'":
                orient = m.Orient.up
            elif r_orient == ",":
                orient = m.Orient.down
            elif r_orient == ";":
                orient = m.Orient.mix
            else:
                orient = m.Orient.mix
        else:
            r_str = r
            orient = m.Orient.mix
        spec = species[r_str]
        spec_orient = (spec, orient)
        spec_orient_list.append(spec_orient)
    return spec_orient_list


def create_reactions_from_dm(
        data_model: Dict, species: Dict[str, m.Species]) -> List[m.Reaction]:
    rxn_dm_list = data_model['mcell']['define_reactions']['reaction_list']
    rxn_list = []
    for rxn_dm in rxn_dm_list:
        # rxn_name = rxn_dm['rxn_name']
        fwd_rate = float(rxn_dm['fwd_rate'])
        # try:
        #     bkwd_rate = float(rxn_dm['bkwd_rate'])
        # except ValueError:
        #     pass
        reactants_str_list = rxn_dm['reactants'].split(" + ")
        products_str_list = rxn_dm['products'].split(" + ")
        r_list = make_spec_orient_list(reactants_str_list, species)
        p_list = make_spec_orient_list(products_str_list, species)
        # rxn_type = rxn_dm['rxn_type']
        rxn_dm = m.Reaction(r_list, p_list, fwd_rate)
        rxn_list.append(rxn_dm)
    return rxn_list


def create_meshobjs_from_dm(dm: Dict):
    meshobj_dm_list = dm['mcell']['geometrical_objects']['object_list']
    meshobj_dict = {}
    for meshobj_dm in meshobj_dm_list:
        name = meshobj_dm['name']
        vert_list = meshobj_dm['vertex_list']
        face_list = meshobj_dm['element_connections']
        meshobj = m.MeshObj(name, vert_list, face_list)
        for reg_dm in meshobj_dm['define_surface_regions']:
            reg_name = reg_dm['name']
            face_list = reg_dm['include_elements']
            m.SurfaceRegion(meshobj, reg_name, face_list)
        meshobj_dict[name] = meshobj
    return meshobj_dict


def create_surface_classes_from_dm(dm: Dict, world, spec_dict):
    sc_dm_list = dm['mcell']['define_surface_classes']['surface_class_list']
    sc_dict = {}
    for sc_dm in sc_dm_list:
        sc_name = sc_dm['name']
        for sc_prop_dm in sc_dm['surface_class_prop_list']:
            if sc_prop_dm['affected_mols'] == 'SINGLE':
                spec_name = sc_prop_dm['molecule']
            else:
                # XXX: Need to add in case for ALL_MOLECULES,
                # ALL_SURFACE_MOLECULES, etc.
                pass
            spec = spec_dict[spec_name]
            surf_class_type = sc_prop_dm['surf_class_type'].lower()
            sc = m.SurfaceClass(sc_name, surf_class_type, spec)
            sc_dict[sc_name] = sc
    return sc_dict


def create_mod_surf_reg_from_dm(dm: Dict, world, sc_dict: Dict, meshobj_dict: Dict):
    mod_sr_list = dm['mcell']['modify_surface_regions']['modify_surface_regions_list']
    for mod_sr in mod_sr_list:
        object_name = mod_sr['object_name']
        region_name = mod_sr['region_name']
        surf_class_name = mod_sr['surf_class_name']
        sc = sc_dict[surf_class_name]
        meshobj = meshobj_dict[object_name]
        for reg in meshobj.regions:
            if reg.reg_name == region_name:
                world.assign_surf_class(sc, reg)
            break


def create_release_sites_from_dm(
        data_model: Dict,
        world,
        meshobjs,
        species: Dict[str, m.Species]):
    rel_site_dm_list = \
            data_model['mcell']['release_sites']['release_site_list']
    rel_site_list = []
    for rel_site_dm in rel_site_dm_list:
        rel_site_name = rel_site_dm['name']
        object_expr = rel_site_dm['object_expr']
        idx = object_expr.find("[")
        if idx > 0:
            object_name = object_expr[:idx]
            reg_name = object_expr[idx+1:-1]
        else:
            object_name = object_expr
            reg_name = ""
        meshobj = meshobjs[object_name]
        spec_name = rel_site_dm['molecule']
        spec = species[spec_name]
        quantity = int(rel_site_dm['quantity'])
        orient = rel_site_dm['orient']
        # XXX: this is really clunky.
        reg = None
        for r in meshobj.regions:
            if r.reg_name == reg_name:
                reg = r
                break
        if orient:
            world.release_into_meshobj(
                    meshobj, spec, quantity, orient=1, region=reg,
                    rel_site_name=rel_site_name)
        else:
            world.release_into_meshobj(
                    meshobj, spec, quantity, region=reg,
                    rel_site_name=rel_site_name)

    return rel_site_list


def create_reaction_data_from_dm(
        data_model: Dict,
        world,
        meshobjs,
        species: Dict[str, m.Species]):
    rxn_out_dm_list = \
            data_model['mcell']['reaction_data_output']['reaction_output_list']
    for rxn_out_dm in rxn_out_dm_list:
        molecule_name = rxn_out_dm['molecule_name']
        spec = species[molecule_name]
        count_location = rxn_out_dm['count_location']
        if count_location == "Object":
            object_name = rxn_out_dm['object_name']
            meshobj = meshobjs[object_name]
            world.add_count(spec, mesh_obj=meshobj)
        elif count_location == "Region":
            region_name = rxn_out_dm['region_name']
            object_name = rxn_out_dm['object_name']
            meshobj = meshobjs[object_name]
            for reg in meshobj.regions:
                if reg.reg_name == region_name:
                    world.add_count(spec, reg=reg)
        else:
            world.add_count(spec)


def create_viz_data_from_dm(
        data_model: Dict,
        world,
        species: Dict[str, m.Species]):
    species_dm_list = data_model['mcell']['define_molecules']['molecule_list']
    export_all = data_model['mcell']['viz_output']['export_all']
    species_list = []
    for species_dm in species_dm_list:
        if export_all or species_dm['export_viz']:
            species_name = species_dm['mol_name']
            spec = species[species_name]
            species_list.append(spec)
    world.add_viz(species_list)


def create_initializations_from_dm(
        data_model: Dict,
        world):
    initialization = data_model['mcell']['initialization']
    iterations = int(initialization['iterations'])
    time_step = float(initialization['time_step'])
    world.set_iterations(iterations)
    world.set_time_step(time_step)

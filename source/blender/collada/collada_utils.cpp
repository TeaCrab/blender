/*
 * ***** BEGIN GPL LICENSE BLOCK *****
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
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
 *
 * Contributor(s): Chingiz Dyussenov, Arystanbek Dyussenov, Nathan Letwory.
 *
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file blender/collada/collada_utils.cpp
 *  \ingroup collada
 */


/* COLLADABU_ASSERT, may be able to remove later */
#include "COLLADABUPlatform.h"

#include "COLLADAFWGeometry.h"
#include "COLLADAFWMeshPrimitive.h"
#include "COLLADAFWMeshVertexData.h"

#include "collada_utils.h"

extern "C" {
#include "DNA_modifier_types.h"
#include "DNA_customdata_types.h"
#include "DNA_object_types.h"
#include "DNA_mesh_types.h"
#include "DNA_scene_types.h"
#include "DNA_armature_types.h"

#include "BLI_math.h"
#include "BLI_linklist.h"

#include "BKE_context.h"
#include "BKE_customdata.h"
#include "BKE_depsgraph.h"
#include "BKE_object.h"
#include "BKE_global.h"
#include "BKE_mesh.h"
#include "BKE_scene.h"
#include "BKE_DerivedMesh.h"

#include "ED_armature.h"

#include "WM_api.h" // XXX hrm, see if we can do without this
#include "WM_types.h"

#include "bmesh.h"
#include "bmesh_tools.h"
}

float bc_get_float_value(const COLLADAFW::FloatOrDoubleArray& array, unsigned int index)
{
	if (index >= array.getValuesCount())
		return 0.0f;

	if (array.getType() == COLLADAFW::MeshVertexData::DATA_TYPE_FLOAT)
		return array.getFloatValues()->getData()[index];
	else 
		return array.getDoubleValues()->getData()[index];
}

// copied from /editors/object/object_relations.c
int bc_test_parent_loop(Object *par, Object *ob)
{
	/* test if 'ob' is a parent somewhere in par's parents */
	
	if (par == NULL) return 0;
	if (ob == par) return 1;
	
	return bc_test_parent_loop(par->parent, ob);
}

// a shortened version of parent_set_exec()
// if is_parent_space is true then ob->obmat will be multiplied by par->obmat before parenting
int bc_set_parent(Object *ob, Object *par, bContext *C, bool is_parent_space)
{
	Object workob;
	Scene *sce = CTX_data_scene(C);
	
	if (!par || bc_test_parent_loop(par, ob))
		return false;

	ob->parent = par;
	ob->partype = PAROBJECT;

	ob->parsubstr[0] = 0;

	if (is_parent_space) {
		float mat[4][4];
		// calc par->obmat
		BKE_object_where_is_calc(sce, par);

		// move child obmat into world space
		mul_m4_m4m4(mat, par->obmat, ob->obmat);
		copy_m4_m4(ob->obmat, mat);
	}
	
	// apply child obmat (i.e. decompose it into rot/loc/size)
	BKE_object_apply_mat4(ob, ob->obmat, 0, 0);

	// compute parentinv
	BKE_object_workob_calc_parent(sce, ob, &workob);
	invert_m4_m4(ob->parentinv, workob.obmat);

	DAG_id_tag_update(&ob->id, OB_RECALC_OB | OB_RECALC_DATA);
	DAG_id_tag_update(&par->id, OB_RECALC_OB);

	/** done once after import */
#if 0
	DAG_relations_tag_update(bmain);
	WM_event_add_notifier(C, NC_OBJECT | ND_TRANSFORM, NULL);
#endif

	return true;
}

Object *bc_add_object(Scene *scene, int type, const char *name)
{
	Object *ob = BKE_object_add_only_object(G.main, type, name);

	ob->data = BKE_object_obdata_add_from_type(G.main, type, name);
	ob->lay = scene->lay;
	DAG_id_tag_update(&ob->id, OB_RECALC_OB | OB_RECALC_DATA | OB_RECALC_TIME);

	BKE_scene_base_select(scene, BKE_scene_base_add(scene, ob));

	return ob;
}

Mesh *bc_get_mesh_copy(Scene *scene, Object *ob, BC_export_mesh_type export_mesh_type, bool apply_modifiers, bool triangulate)
{
	Mesh *tmpmesh;
	CustomDataMask mask = CD_MASK_MESH;
	Mesh *mesh = (Mesh *)ob->data;
	DerivedMesh *dm = NULL;
	if (apply_modifiers) {
		switch (export_mesh_type) {
			case BC_MESH_TYPE_VIEW:
			{
				dm = mesh_create_derived_view(scene, ob, mask);
				break;
			}
			case BC_MESH_TYPE_RENDER:
			{
				dm = mesh_create_derived_render(scene, ob, mask);
				break;
			}
		}
	}
	else {
		dm = mesh_create_derived((Mesh *)ob->data, NULL);
	}

	tmpmesh = BKE_mesh_add(G.main, "ColladaMesh"); // name is not important here
	DM_to_mesh(dm, tmpmesh, ob, CD_MASK_MESH, true);
	tmpmesh->flag = mesh->flag;

	if (triangulate) {
		bc_triangulate_mesh(tmpmesh);
	}
	BKE_mesh_tessface_ensure(tmpmesh);
	return tmpmesh;
}

Object *bc_get_assigned_armature(Object *ob)
{
	Object *ob_arm = NULL;

	if (ob->parent && ob->partype == PARSKEL && ob->parent->type == OB_ARMATURE) {
		ob_arm = ob->parent;
	}
	else {
		ModifierData *mod;
		for (mod = (ModifierData *)ob->modifiers.first; mod; mod = mod->next) {
			if (mod->type == eModifierType_Armature) {
				ob_arm = ((ArmatureModifierData *)mod)->object;
			}
		}
	}

	return ob_arm;
}

// Returns the highest selected ancestor
// returns NULL if no ancestor is selected
// IMPORTANT: This function expects that
// all exported objects have set:
// ob->id.tag & LIB_TAG_DOIT
Object *bc_get_highest_selected_ancestor_or_self(LinkNode *export_set, Object *ob) 
{
	Object *ancestor = ob;
	while (ob->parent && bc_is_marked(ob->parent)) {
		ob = ob->parent;
		ancestor = ob;
	}
	return ancestor;
}


bool bc_is_base_node(LinkNode *export_set, Object *ob)
{
	Object *root = bc_get_highest_selected_ancestor_or_self(export_set, ob);
	return (root == ob);
}

bool bc_is_in_Export_set(LinkNode *export_set, Object *ob)
{
	return (BLI_linklist_index(export_set, ob) != -1);
}

bool bc_has_object_type(LinkNode *export_set, short obtype)
{
	LinkNode *node;
	
	for (node = export_set; node; node = node->next) {
		Object *ob = (Object *)node->link;
		/* XXX - why is this checking for ob->data? - we could be looking for empties */
		if (ob->type == obtype && ob->data) {
			return true;
		}
	}
	return false;
}

int bc_is_marked(Object *ob)
{
	return ob && (ob->id.tag & LIB_TAG_DOIT);
}

void bc_remove_mark(Object *ob)
{
	ob->id.tag &= ~LIB_TAG_DOIT;
}

void bc_set_mark(Object *ob)
{
	ob->id.tag |= LIB_TAG_DOIT;
}

// Use bubble sort algorithm for sorting the export set
void bc_bubble_sort_by_Object_name(LinkNode *export_set)
{
	bool sorted = false;
	LinkNode *node;
	for (node = export_set; node->next && !sorted; node = node->next) {

		sorted = true;
		
		LinkNode *current;
		for (current = export_set; current->next; current = current->next) {
			Object *a = (Object *)current->link;
			Object *b = (Object *)current->next->link;

			if (strcmp(a->id.name, b->id.name) > 0) {
				current->link       = b;
				current->next->link = a;
				sorted = false;
			}
			
		}
	}
}

/* Check if a bone is the top most exportable bone in the bone hierarchy. 
 * When deform_bones_only == false, then only bones with NO parent 
 * can be root bones. Otherwise the top most deform bones in the hierarchy
 * are root bones.
 */
bool bc_is_root_bone(Bone *aBone, bool deform_bones_only)
{
	if (deform_bones_only) {
		Bone *root = NULL;
		Bone *bone = aBone;
		while (bone) {
			if (!(bone->flag & BONE_NO_DEFORM))
				root = bone;
			bone = bone->parent;
		}
		return (aBone == root);
	}
	else
		return !(aBone->parent);
}

int bc_get_active_UVLayer(Object *ob)
{
	Mesh *me = (Mesh *)ob->data;
	return CustomData_get_active_layer_index(&me->fdata, CD_MTFACE);
}

std::string bc_url_encode(std::string data)
{
	/* XXX We probably do not need to do a full encoding.
	 * But in case that is necessary,then it can be added here.
	 */
	return bc_replace_string(data,"#", "%23");
}

std::string bc_replace_string(std::string data, const std::string& pattern,
                              const std::string& replacement)
{
	size_t pos = 0;
	while ((pos = data.find(pattern, pos)) != std::string::npos) {
		data.replace(pos, pattern.length(), replacement);
		pos += replacement.length();
	}
	return data;
}

/**
 * Calculate a rescale factor such that the imported scene's scale
 * is preserved. I.e. 1 meter in the import will also be
 * 1 meter in the current scene.
 */

void bc_match_scale(Object *ob, UnitConverter &bc_unit, bool scale_to_scene)
{
	if (scale_to_scene) {
		mul_m4_m4m4(ob->obmat, bc_unit.get_scale(), ob->obmat);
	}
	mul_m4_m4m4(ob->obmat, bc_unit.get_rotation(), ob->obmat);
	BKE_object_apply_mat4(ob, ob->obmat, 0, 0);
}

void bc_match_scale(std::vector<Object *> *objects_done, 
	                UnitConverter &bc_unit,
	                bool scale_to_scene)
{
	for (std::vector<Object *>::iterator it = objects_done->begin();
			it != objects_done->end();
			++it) 
	{
		Object *ob = *it;
		if (ob -> parent == NULL) {
			bc_match_scale(*it, bc_unit, scale_to_scene);
		}
	}
}

void bc_triangulate_mesh(Mesh *me)
{
	bool use_beauty  = false;
	bool tag_only    = false;
	int  quad_method = MOD_TRIANGULATE_QUAD_SHORTEDGE; /* XXX: The triangulation method selection could be offered in the UI */

	const struct BMeshCreateParams bm_create_params = {0};
	BMesh *bm = BM_mesh_create(
	        &bm_mesh_allocsize_default,
	        &bm_create_params);
	BMeshFromMeshParams bm_from_me_params = {0};
	bm_from_me_params.calc_face_normal = true;
	BM_mesh_bm_from_me(bm, me, &bm_from_me_params);
	BM_mesh_triangulate(bm, quad_method, use_beauty, tag_only, NULL, NULL, NULL);

	BMeshToMeshParams bm_to_me_params = {0};
	BM_mesh_bm_to_me(bm, me, &bm_to_me_params);
	BM_mesh_free(bm);
}

/*
 * A bone is a leaf when it has no children or all children are not connected.
 */
bool bc_is_leaf_bone(Bone *bone)
{
	for (Bone *child = (Bone *)bone->childbase.first; child; child = child->next) {
		if (child->flag & BONE_CONNECTED)
			return false;
	}
	return true;
}

EditBone *bc_get_edit_bone(bArmature * armature, char *name) {
	EditBone  *eBone;

	for (eBone = (EditBone *)armature->edbo->first; eBone; eBone = eBone->next) {
		if (STREQ(name, eBone->name))
			return eBone;
	}

	return NULL;

}
int bc_set_layer(int bitfield, int layer)
{
	return bc_set_layer(bitfield, layer, true); /* enable */
}

int bc_set_layer(int bitfield, int layer, bool enable)
{
	int bit = 1u << layer;

	if (enable)
		bitfield |= bit;
	else
		bitfield &= ~bit;

	return bitfield;
}

/*
 | This method creates a new extension map when needed.
 | Note: The ~BoneExtensionManager destructor takes care
 | to delete the created maps when the manager is removed.
*/
BoneExtensionMap &BoneExtensionManager::getExtensionMap(bArmature *armature)
{
	std::string key = armature->id.name;
	BoneExtensionMap *result = extended_bone_maps[key];
	if (result == NULL)
	{
		result = new BoneExtensionMap();
		extended_bone_maps[key] = result;
	}
	return *result;
}

BoneExtensionManager::~BoneExtensionManager()
{
	std::map<std::string, BoneExtensionMap *>::iterator map_it;
	for (map_it = extended_bone_maps.begin(); map_it != extended_bone_maps.end(); ++map_it)
	{
		BoneExtensionMap *extended_bones = map_it->second;
		for (BoneExtensionMap::iterator ext_it = extended_bones->begin(); ext_it != extended_bones->end(); ++ext_it) {
			if (ext_it->second != NULL)
				delete ext_it->second;
		}
		extended_bones->clear();
		delete extended_bones;
	}
}

/**
 * BoneExtended is a helper class needed for the Bone chain finder
 * See ArmatureImporter::fix_leaf_bones()
 * and ArmatureImporter::connect_bone_chains()
 */

BoneExtended::BoneExtended(EditBone *aBone)
{
	this->set_name(aBone->name);
	this->chain_length = 0;
	this->is_leaf = false;
	this->tail[0] = 0.0f;
	this->tail[1] = 0.5f;
	this->tail[2] = 0.0f;
	this->use_connect = -1;
	this->roll = 0;
	this->bone_layers = 0;

	this->has_custom_tail = false;
	this->has_custom_roll = false;
}

char *BoneExtended::get_name()
{
	return name;
}

void BoneExtended::set_name(char *aName)
{
	BLI_strncpy(name, aName, MAXBONENAME);
}

int BoneExtended::get_chain_length()
{
	return chain_length;
}

void BoneExtended::set_chain_length(const int aLength)
{
	chain_length = aLength;
}

void BoneExtended::set_leaf_bone(bool state)
{
	is_leaf = state;
}

bool BoneExtended::is_leaf_bone()
{
	return is_leaf;
}

void BoneExtended::set_roll(float roll)
{
	this->roll = roll;
	this->has_custom_roll = true;
}

bool BoneExtended::has_roll()
{
	return this->has_custom_roll;
}

float BoneExtended::get_roll()
{
	return this->roll;
}

void BoneExtended::set_tail(float vec[])
{
	this->tail[0] = vec[0];
	this->tail[1] = vec[1];
	this->tail[2] = vec[2];
	this->has_custom_tail = true;
}

bool BoneExtended::has_tail()
{
	return this->has_custom_tail;
}

float *BoneExtended::get_tail()
{
	return this->tail;
}

inline bool isInteger(const std::string & s)
{
	if (s.empty() || ((!isdigit(s[0])) && (s[0] != '-') && (s[0] != '+'))) return false;

	char * p;
	strtol(s.c_str(), &p, 10);

	return (*p == 0);
}

void BoneExtended::set_bone_layers(std::string layerString, std::vector<std::string> &layer_labels)
{
	std::stringstream ss(layerString);
	std::string layer;
	int pos;

	while (ss >> layer) {

		/* Blender uses numbers to specify layers*/
		if (isInteger(layer))
		{
			pos = atoi(layer.c_str());
			if (pos >= 0 && pos < 32) {
				this->bone_layers = bc_set_layer(this->bone_layers, pos);
				continue;
			}
		}

		/* layer uses labels (not supported by blender). Map to layer numbers:*/
		pos = find(layer_labels.begin(), layer_labels.end(), layer) - layer_labels.begin();
		if (pos >= layer_labels.size()) {
			layer_labels.push_back(layer); /* remember layer number for future usage*/
		}

		if (pos > 31)
		{
			fprintf(stderr, "Too many layers in Import. Layer %s mapped to Blender layer 31\n", layer.c_str());
			pos = 31;
		}

		/* If numeric layers and labeled layers are used in parallel (unlikely),
		 * we get a potential mixup. Just leave as is for now.
		 */
		this->bone_layers = bc_set_layer(this->bone_layers, pos);

	}
}

std::string BoneExtended::get_bone_layers(int bitfield)
{
	std::string result = "";
	std::string sep = "";
	int bit = 1u;

	std::ostringstream ss;
	for (int i = 0; i < 32; i++)
	{
		if (bit & bitfield)
		{
			ss << sep << i;
			sep = " ";
		}
		bit = bit << 1;
	}
	return ss.str();
}

int BoneExtended::get_bone_layers()
{
	return (bone_layers == 0) ? 1 : bone_layers; // ensure that the bone is in at least one bone layer!
}


void BoneExtended::set_use_connect(int use_connect)
{
	this->use_connect = use_connect;
}

int BoneExtended::get_use_connect()
{
	return this->use_connect;
}

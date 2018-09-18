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
 * ***** END GPL LICENSE BLOCK *****
 */

/** \file gizmo_utils.c
 *  \ingroup edutil
 *
 * \name Generic Gizmo Utilities.
 */

#include <string.h>

#include "BLI_utildefines.h"

#include "BKE_context.h"

#include "DNA_workspace_types.h"

#include "WM_types.h"
#include "WM_api.h"
#include "WM_toolsystem.h"

#include "ED_gizmo_utils.h"

bool ED_gizmo_poll_or_unlink_delayed_from_operator(
        const bContext *C, wmGizmoGroupType *gzgt,
        const char *idname)
{
	wmOperator *op = WM_operator_last_redo(C);
	if (op == NULL || !STREQ(op->type->idname, idname)) {
		WM_gizmo_group_type_unlink_delayed_ptr(gzgt);
		return false;
	}
	return true;
}

/** Can use this as poll function directly. */
bool ED_gizmo_poll_or_unlink_delayed_from_tool(const bContext *C, wmGizmoGroupType *gzgt)
{
	bToolRef_Runtime *tref_rt = WM_toolsystem_runtime_from_context((bContext *)C);
	if ((tref_rt == NULL) ||
	    !STREQ(gzgt->idname, tref_rt->gizmo_group))
	{
		WM_gizmo_group_type_unlink_delayed_ptr(gzgt);
		return false;
	}
	return true;
}
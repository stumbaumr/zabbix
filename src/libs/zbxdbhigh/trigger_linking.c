\/*
** Zabbix
** Copyright (C) 2001-2021 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "trigger_linking.h"
#include "db.h"

typedef struct
{
	zbx_uint64_t	*new_triggerid;
	zbx_uint64_t	*cur_triggerid;
	zbx_uint64_t	hostid;
	zbx_uint64_t	triggerid;
	char		*description;
	char		*expression;
	char		*recovery_expression;
	unsigned char	recovery_mode;
	unsigned char	status;
	unsigned char	type;
	unsigned char	priority;
	char		*comments;
	char		*url;
	unsigned char	flags;
	unsigned char	correlation_mode;
	char		*correlation_tag;
	unsigned char	manual_close;
	char		*opdata;
	unsigned char	discover;
	char		*event_name;
	char		**error;

	zbx_uint64_t templateid;
}
zbx_trigger_copy_t;

ZBX_PTR_VECTOR_DECL(trigger_copies_templates, zbx_trigger_copy_t *)
ZBX_PTR_VECTOR_IMPL(trigger_copies_templates, zbx_trigger_copy_t *)

ZBX_PTR_VECTOR_DECL(trigger_copies_insert, zbx_trigger_copy_t *)
ZBX_PTR_VECTOR_IMPL(trigger_copies_insert, zbx_trigger_copy_t *)

/* TARGET HOST TRIGGER DATA */
typedef struct
{
	zbx_uint64_t	triggerid;
	char		*description;
	char		*expression;
	char		*recovery_expression;

	zbx_uint64_t	templateid;
	zbx_uint64_t	flags_orig;
	unsigned char	flags;
	unsigned char	recovery_mode_orig;
	unsigned char	recovery_mode;
	unsigned char	correlation_mode_orig;
	unsigned char	correlation_mode;
	unsigned char	manual_close_orig;
	unsigned char	manual_close;
	char		*opdata_orig;
	char		*opdata;
	unsigned char	discover_orig;
	unsigned char	discover;
	char		*event_name_orig;
	char		*event_name;

#define ZBX_FLAG_LINK_FUNCTION_UNSET			__UINT64_C(0x00)
#define ZBX_FLAG_LINK_FUNCTION_UPDATE_FLAGS		__UINT64_C(0x01)
#define ZBX_FLAG_LINK_FUNCTION_UPDATE_RECOVERY_MODE	__UINT64_C(0x02)
#define ZBX_FLAG_LINK_FUNCTION_UPDATE_CORRELATION_MODE	__UINT64_C(0x04)
#define ZBX_FLAG_LINK_FUNCTION_UPDATE_MANUAL_CLOSE	__UINT64_C(0x08)
#define ZBX_FLAG_LINK_FUNCTION_UPDATE_OPDATA		__UINT64_C(0x10)
#define ZBX_FLAG_LINK_FUNCTION_UPDATE_DISCOVER		__UINT64_C(0x20)
#define ZBX_FLAG_LINK_FUNCTION_UPDATE_EVENT_NAME		__UINT64_C(0x40)

#define ZBX_FLAG_LINK_TRIGGER_UPDATE                                                                    \
	(ZBX_FLAG_LINK_FUNCTION_UPDATE_FLAGS | ZBX_FLAG_LINK_FUNCTION_UPDATE_RECOVERY_MODE |            \
	ZBX_FLAG_LINK_FUNCTION_UPDATE_CORRELATION_MODE | ZBX_FLAG_LINK_FUNCTION_UPDATE_MANUAL_CLOSE |   \
	ZBX_FLAG_LINK_FUNCTION_UPDATE_OPDATA | ZBX_FLAG_LINK_FUNCTION_UPDATE_DISCOVER |                 \
	ZBX_FLAG_LINK_FUNCTION_UPDATE_EVENT_NAME)

	zbx_uint64_t	update_flags;
}
zbx_target_host_trigger_entry_t;

ZBX_PTR_VECTOR_DECL(target_host_trigger_data, zbx_target_host_trigger_entry_t *);
ZBX_PTR_VECTOR_IMPL(target_host_trigger_data, zbx_target_host_trigger_entry_t *);

static unsigned	zbx_host_triggers_main_data_hash_func(const void *data)
{
	const zbx_target_host_trigger_entry_t	* trigger_entry = (const zbx_target_host_trigger_entry_t *)data;

	return ZBX_DEFAULT_UINT64_HASH_ALGO(&((trigger_entry)->triggerid), sizeof((trigger_entry)->triggerid),
			ZBX_DEFAULT_HASH_SEED);
}

static int	zbx_host_triggers_main_data_compare_func(const void *d1, const void *d2)
{
	const zbx_target_host_trigger_entry_t	*trigger_entry_1 = (const zbx_target_host_trigger_entry_t *)d1;
	const zbx_target_host_trigger_entry_t	*trigger_entry_2 = (const zbx_target_host_trigger_entry_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL(trigger_entry_1->triggerid, trigger_entry_2->triggerid);

	return 0;
}

static void	zbx_host_triggers_main_data_clean(zbx_hashset_t *h)
{
	zbx_hashset_iter_t		iter;
	zbx_target_host_trigger_entry_t	**trigger_entry;

	zbx_hashset_iter_reset(h, &iter);

	while (NULL != (trigger_entry = (zbx_target_host_trigger_entry_t **)zbx_hashset_iter_next(&iter)))
	{
		/*zbx_free((*audit_entry)->name);*/
		/*zbx_free(*audit_entry);*/
	}

	zbx_hashset_destroy(h);
}

/* TRIGGER FUNCTIONS DATA */

typedef struct zbx_trigger_functions_entry
{
	zbx_uint64_t		triggerid;

	zbx_vector_str_t	functionids;
	zbx_vector_uint64_t	itemids;
	zbx_vector_str_t	itemkeys;
	zbx_vector_str_t	parameters;
	zbx_vector_str_t	names;

} zbx_trigger_functions_entry_t;

static unsigned	zbx_triggers_functions_hash_func(const void *data)
{
	const zbx_trigger_functions_entry_t	*trigger_entry = (const zbx_trigger_functions_entry_t *)data;

	return ZBX_DEFAULT_UINT64_HASH_ALGO(&((trigger_entry)->triggerid), sizeof((trigger_entry)->triggerid),
			ZBX_DEFAULT_HASH_SEED);
}

static int	zbx_triggers_functions_compare_func(const void *d1, const void *d2)
{
	const zbx_trigger_functions_entry_t	*trigger_entry_1 = (const zbx_trigger_functions_entry_t *)d1;
	const zbx_trigger_functions_entry_t	*trigger_entry_2 = (const zbx_trigger_functions_entry_t *)d2;

	ZBX_RETURN_IF_NOT_EQUAL((trigger_entry_1)->triggerid, (trigger_entry_2)->triggerid);

	return 0;
}

static void	zbx_triggers_functions_clean(zbx_hashset_t *h)
{
	zbx_hashset_iter_t		iter;
	zbx_trigger_functions_entry_t	**trigger_entry;

	zbx_hashset_iter_reset(h, &iter);

	while (NULL != (trigger_entry = (zbx_trigger_functions_entry_t **)zbx_hashset_iter_next(&iter)))
	{
		/*zbx_vector_str_destroy(&((*trigger_entry)->functionids));
		zbx_vector_uint64_destroy(&((*trigger_entry)->itemids));
		zbx_vector_str_destroy(&((*trigger_entry)->itemkeys));
		zbx_vector_str_destroy(&((*trigger_entry)->parameters));
		zbx_vector_str_destroy(&((*trigger_entry)->names));
		zbx_free(*trigger_entry);*/
	}
	zbx_hashset_destroy(h);
}

/* TRIGGER DESCRIPTIONS MAP */

typedef struct zbx_trigger_descriptions_entry
{
	const char		*description;
	zbx_vector_uint64_t	triggerids;
} zbx_trigger_descriptions_entry_t;

static zbx_hash_t	zbx_triggers_descriptions_hash_func(const void *data)
{
	const zbx_trigger_descriptions_entry_t	*trigger_entry = (const zbx_trigger_descriptions_entry_t * )data;

	return  ZBX_DEFAULT_STRING_HASH_ALGO(trigger_entry->description, strlen(trigger_entry->description),
			ZBX_DEFAULT_HASH_SEED);
}

static int	zbx_triggers_descriptions_compare_func(const void *d1, const void *d2)
{
	const zbx_trigger_descriptions_entry_t	*trigger_entry_1 = (const zbx_trigger_descriptions_entry_t * )d1;
	const zbx_trigger_descriptions_entry_t	*trigger_entry_2 = (const zbx_trigger_descriptions_entry_t * )d2;

	return strcmp((trigger_entry_1)->description, (trigger_entry_2)->description);
}

static void	zbx_templates_triggers_descriptions_clean(zbx_hashset_t *x)
{
	zbx_hashset_iter_t			iter;
	zbx_trigger_descriptions_entry_t	**trigger_entry;

	zbx_hashset_iter_reset(x, &iter);

	while (NULL != (trigger_entry = (zbx_trigger_descriptions_entry_t **)zbx_hashset_iter_next(&iter)))
	{
		/*zbx_free((*audit_entry)->name);*/
		/*zbx_free(*audit_entry);*/
	}

	zbx_hashset_destroy(x);
}



/******************************************************************************
 *                                                                            *
 * Function: DBresolve_template_trigger_dependencies                          *
 *                                                                            *
 * Purpose: resolves trigger dependencies for the specified triggers based on *
 *          host and linked templates                                         *
 *                                                                            *
 * Parameters: hostid    - [IN] host identificator from database              *
 *             trids     - [IN] array of trigger identifiers from database    *
 *             trids_num - [IN] trigger count in trids array                  *
 *             links     - [OUT] pairs of trigger dependencies  (down,up)     *
 *                                                                            *
 ******************************************************************************/
static void	DBresolve_template_trigger_dependencies(zbx_uint64_t hostid, const zbx_uint64_t *trids, int trids_num,
		zbx_vector_uint64_pair_t *links)
{
	DB_RESULT			result;
	DB_ROW				row;
	zbx_uint64_pair_t		map_id, dep_list_id;
	char				*sql = NULL;
	size_t				sql_alloc = 512, sql_offset;
	zbx_vector_uint64_pair_t	dep_list_ids, map_ids;
	zbx_vector_uint64_t		all_templ_ids;
	zbx_uint64_t			templateid_down, templateid_up,
					triggerid_down, triggerid_up,
					hst_triggerid, tpl_triggerid;
	int				i, j;

	zbx_vector_uint64_create(&all_templ_ids);
	zbx_vector_uint64_pair_create(&dep_list_ids);
	zbx_vector_uint64_pair_create(links);
	sql = (char *)zbx_malloc(sql, sql_alloc);

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct td.triggerid_down,td.triggerid_up"
			" from triggers t,trigger_depends td"
			" where t.templateid in (td.triggerid_up,td.triggerid_down) and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.triggerid", trids, trids_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(dep_list_id.first, row[0]);
		ZBX_STR2UINT64(dep_list_id.second, row[1]);
		zbx_vector_uint64_pair_append(&dep_list_ids, dep_list_id);
		zbx_vector_uint64_append(&all_templ_ids, dep_list_id.first);
		zbx_vector_uint64_append(&all_templ_ids, dep_list_id.second);
	}
	DBfree_result(result);

	if (0 == dep_list_ids.values_num)	/* not all trigger template have a dependency trigger */
	{
		zbx_vector_uint64_destroy(&all_templ_ids);
		zbx_vector_uint64_pair_destroy(&dep_list_ids);
		zbx_free(sql);
		return;
	}

	zbx_vector_uint64_pair_create(&map_ids);
	zbx_vector_uint64_sort(&all_templ_ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);
	zbx_vector_uint64_uniq(&all_templ_ids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	sql_offset = 0;
	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.triggerid,t.templateid"
			" from triggers t,functions f,items i"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and i.hostid=" ZBX_FS_UI64
				" and",
				hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.templateid", all_templ_ids.values,
			all_templ_ids.values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(map_id.first, row[0]);
		ZBX_DBROW2UINT64(map_id.second, row[1]);
		zbx_vector_uint64_pair_append(&map_ids, map_id);
	}
	DBfree_result(result);

	zbx_free(sql);
	zbx_vector_uint64_destroy(&all_templ_ids);

	for (i = 0; i < dep_list_ids.values_num; i++)
	{
		templateid_down = dep_list_ids.values[i].first;
		templateid_up = dep_list_ids.values[i].second;

		/* Convert template ids to corresponding trigger ids.         */
		/* If template trigger depends on host trigger rather than    */
		/* template trigger then up id conversion will fail and the   */
		/* original value (host trigger id) will be used as intended. */
		triggerid_down = 0;
		triggerid_up = templateid_up;

		for (j = 0; j < map_ids.values_num; j++)
		{
			hst_triggerid = map_ids.values[j].first;
			tpl_triggerid = map_ids.values[j].second;

			if (tpl_triggerid == templateid_down)
				triggerid_down = hst_triggerid;

			if (tpl_triggerid == templateid_up)
				triggerid_up = hst_triggerid;
		}

		if (0 != triggerid_down)
		{
			zbx_uint64_pair_t	link = {triggerid_down, triggerid_up};

			zbx_vector_uint64_pair_append(links, link);
		}
	}

	zbx_vector_uint64_pair_destroy(&map_ids);
	zbx_vector_uint64_pair_destroy(&dep_list_ids);
}

/******************************************************************************
 *                                                                            *
 * Function: DBadd_template_dependencies_for_new_triggers                     *
 *                                                                            *
 * Purpose: update trigger dependencies for specified host                    *
 *                                                                            *
 * Parameters: hostid    - [IN] host identificator from database              *
 *             trids     - [IN] array of trigger identifiers from database    *
 *             trids_num - [IN] trigger count in trids array                  *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
static int	DBadd_template_dependencies_for_new_triggers(zbx_uint64_t hostid, zbx_uint64_t *trids, int trids_num)
{
	int				i;
	zbx_uint64_t			triggerdepid;
	zbx_db_insert_t			db_insert;
	zbx_vector_uint64_pair_t	links;

	if (0 == trids_num)
		return SUCCEED;

	DBresolve_template_trigger_dependencies(hostid, trids, trids_num, &links);

	if (0 < links.values_num)
	{
		triggerdepid = DBget_maxid_num("trigger_depends", links.values_num);

		zbx_db_insert_prepare(&db_insert, "trigger_depends", "triggerdepid", "triggerid_down", "triggerid_up",
				NULL);

		for (i = 0; i < links.values_num; i++)
		{
			zbx_db_insert_add_values(&db_insert, triggerdepid++, links.values[i].first,
					links.values[i].second);
		}

		zbx_db_insert_execute(&db_insert);
		zbx_db_insert_clean(&db_insert);
	}

	zbx_vector_uint64_pair_destroy(&links);

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_trigger_tags                                     *
 *                                                                            *
 * Purpose: copies tags from template triggers to created/linked triggers     *
 *                                                                            *
 * Parameters: new_triggerids - the created trigger ids                        *
 *             cur_triggerids - the linked trigfer ids                         *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *                                                                            *
 ******************************************************************************/
static int	DBcopy_template_trigger_tags(const zbx_vector_uint64_t *new_triggerids,
		const zbx_vector_uint64_t *cur_triggerids)
{
	DB_RESULT		result;
	DB_ROW			row;
	char			*sql = NULL;
	size_t			sql_alloc = 0, sql_offset = 0;
	int			i;
	zbx_vector_uint64_t	triggerids;
	zbx_uint64_t		triggerid;
	zbx_db_insert_t		db_insert;

	if (0 == new_triggerids->values_num && 0 == cur_triggerids->values_num)
		return SUCCEED;

	zbx_vector_uint64_create(&triggerids);
	zbx_vector_uint64_reserve(&triggerids, new_triggerids->values_num + cur_triggerids->values_num);

	if (0 != cur_triggerids->values_num)
	{
		/* remove tags from host triggers that were linking to template triggers */
		zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "delete from trigger_tag where");
		DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "triggerid", cur_triggerids->values,
				cur_triggerids->values_num);
		DBexecute("%s", sql);

		sql_offset = 0;

		for (i = 0; i < cur_triggerids->values_num; i++)
			zbx_vector_uint64_append(&triggerids, cur_triggerids->values[i]);
	}

	for (i = 0; i < new_triggerids->values_num; i++)
		zbx_vector_uint64_append(&triggerids, new_triggerids->values[i]);

	zbx_vector_uint64_sort(&triggerids, ZBX_DEFAULT_UINT64_COMPARE_FUNC);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select t.triggerid,tt.tag,tt.value"
			" from trigger_tag tt,triggers t"
			" where tt.triggerid=t.templateid"
			" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.triggerid", triggerids.values, triggerids.values_num);

	result = DBselect("%s", sql);

	zbx_db_insert_prepare(&db_insert, "trigger_tag", "triggertagid", "triggerid", "tag", "value", NULL);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(triggerid, row[0]);

		zbx_db_insert_add_values(&db_insert, __UINT64_C(0), triggerid, row[1], row[2]);
	}
	DBfree_result(result);

	zbx_free(sql);

	zbx_db_insert_autoincrement(&db_insert, "triggertagid");
	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	zbx_vector_uint64_destroy(&triggerids);

	return SUCCEED;
}

static void	get_trigger_funcs(zbx_vector_uint64_t *triggerids, zbx_hashset_t *funcs_res)
{
	char		*sql = NULL;
	size_t		sql_alloc = 256, sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;

	if (0 == triggerids->values_num)
		return;

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select f.triggerid,f.functionid,f.parameter,i.itemid,i.key_"
				" from functions f,items i"
				" where "
				" i.itemid=f.itemid"
				" and");

	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "f.triggerid", triggerids->values,
			triggerids->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_trigger_functions_entry_t	*found, temp_t;
		zbx_uint64_t			itemid_temp_var;

		zabbix_log(LOG_LEVEL_INFORMATION, "get_trigger_funcs next: %s", row[0]);
		ZBX_STR2UINT64(temp_t.triggerid, row[0]);
		ZBX_STR2UINT64(itemid_temp_var, row[3]);

		if (NULL != (found =  (zbx_trigger_functions_entry_t *)zbx_hashset_search(funcs_res,
				&temp_t)))
		{
			zbx_vector_str_append(&(found->functionids), zbx_strdup(NULL, row[1]));
			zbx_vector_uint64_append(&(found->itemids), itemid_temp_var);
			zbx_vector_str_append(&(found->itemkeys), zbx_strdup(NULL, row[4]));
			zbx_vector_str_append(&(found->parameters), zbx_strdup(NULL, row[2]));
		}
		else
		{
			zbx_vector_str_t		functionids_local, itemkeys_local, parameters_local;
			zbx_vector_uint64_t		itemids_local;
			zbx_trigger_functions_entry_t	local_temp_t;

			zbx_vector_str_create(&functionids_local);
			zbx_vector_uint64_create(&itemids_local);
			zbx_vector_str_create(&itemkeys_local);
			zbx_vector_str_create(&parameters_local);

			zbx_vector_str_append(&functionids_local, zbx_strdup(NULL, row[1]));
			zbx_vector_uint64_append(&itemids_local, itemid_temp_var);
			zbx_vector_str_append(&itemkeys_local, zbx_strdup(NULL, row[4]));
			zbx_vector_str_append(&parameters_local, zbx_strdup(NULL, row[2]));

			/* zbx_vector_str_append(&functionids_local, row[1]); */
			/* zbx_vector_str_append(&itemkeys_local, row[4]); */
			/* zbx_vector_str_append(&parameters_local, row[2]); */

			/*local_temp_t = (zbx_trigger_functions_entry_t *)zbx_malloc(NULL,
				sizeof(zbx_trigger_functions_entry_t));*/
			local_temp_t.triggerid = temp_t.triggerid;
			local_temp_t.functionids = functionids_local;
			local_temp_t.itemids = itemids_local;
			local_temp_t.itemkeys = itemkeys_local;
			local_temp_t.parameters = parameters_local;
			zbx_hashset_insert(funcs_res, &local_temp_t, sizeof(local_temp_t));
			zabbix_log(LOG_LEVEL_INFORMATION, "get_trigger_funcs not found: ->%lu<-",
					local_temp_t.triggerid);
		}
	}

	zbx_free(sql);
	DBfree_result(result);

	{
		zbx_hashset_iter_t		iter1, iter2;
		zbx_trigger_functions_entry_t	*fo;

		zbx_hashset_iter_reset(funcs_res, &iter1);
		zbx_hashset_iter_reset(funcs_res, &iter2);

		while (NULL != (fo = (zbx_trigger_functions_entry_t*)zbx_hashset_iter_next(&iter1)))
			zabbix_log(LOG_LEVEL_INFORMATION, "DIAMON1: %lu", fo->triggerid);

		while (NULL != (fo =  (zbx_trigger_functions_entry_t*)zbx_hashset_iter_next(&iter2)))
			zabbix_log(LOG_LEVEL_INFORMATION, "DIAMON2: %lu", fo->triggerid);
	}
}

static void	get_templates_triggers_data(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids,
		zbx_vector_trigger_copies_templates_t *trigger_copies_templates,
		zbx_vector_str_t *templates_triggers_descriptions,
		zbx_vector_uint64_t *temp_templates_triggerids, int *status_res)
{
	char			*sql = NULL;
	size_t			sql_alloc = 512, sql_offset = 0;
	zbx_trigger_copy_t	*trigger_copy;
	DB_RESULT		result;
	DB_ROW			row;

	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct t.triggerid,t.description,t.expression,t.status,"
				"t.type,t.priority,t.comments,t.url,t.flags,t.recovery_expression,t.recovery_mode,"
				"t.correlation_mode,t.correlation_tag,t.manual_close,t.opdata,t.discover,t.event_name"
			" from triggers t,functions f,items i"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", templateids->values, templateids->values_num);

	result = DBselect("%s", sql);

	zbx_free(sql);

	while (SUCCEED == *status_res && NULL != (row = DBfetch(result)))
	{
		trigger_copy = (zbx_trigger_copy_t *)zbx_malloc(NULL, sizeof(zbx_trigger_copy_t));
		trigger_copy->hostid = hostid;
		ZBX_STR2UINT64(trigger_copy->triggerid, row[0]);
		trigger_copy->templateid=trigger_copy->triggerid;
		trigger_copy->description = zbx_strdup(NULL, row[1]);
		trigger_copy->expression = zbx_strdup(NULL, row[2]);
		trigger_copy->recovery_expression = zbx_strdup(NULL, row[9]);
		trigger_copy->recovery_mode = (unsigned char)atoi(row[10]);
		trigger_copy->status = (unsigned char)atoi(row[3]);
		trigger_copy->type = (unsigned char)atoi(row[4]);
		trigger_copy->priority = (unsigned char)atoi(row[5]);
		trigger_copy->comments = zbx_strdup(NULL, row[6]);
		trigger_copy->url = zbx_strdup(NULL, row[7]);
		trigger_copy->flags = (unsigned char)atoi(row[8]);
		zabbix_log(LOG_LEVEL_INFORMATION, "KRONEG ALPHA 0000: %u", trigger_copy->flags);
		zabbix_log(LOG_LEVEL_INFORMATION, "KRONEG ALPHA S: %s", row[8]);
		zabbix_log(LOG_LEVEL_INFORMATION, "KRONEG ALPHA S: %d", atoi(row[8]));
		trigger_copy->correlation_mode = (unsigned char)atoi(row[11]);
		trigger_copy->correlation_tag = zbx_strdup(NULL, row[12]);
		trigger_copy->manual_close = (unsigned char)atoi(row[13]);
		trigger_copy->opdata = zbx_strdup(NULL, row[14]);
		trigger_copy->discover = (unsigned char)atoi(row[15]);
		trigger_copy->event_name = zbx_strdup(NULL, row[16]);

		zbx_vector_trigger_copies_templates_append(trigger_copies_templates, trigger_copy);
		zbx_vector_str_append(templates_triggers_descriptions, trigger_copy->description);

		zabbix_log(LOG_LEVEL_INFORMATION, "get_templates_triggers_data, saving next description: ->%s<-",
				trigger_copy->description);
		zbx_vector_uint64_append(temp_templates_triggerids, trigger_copy->triggerid);
	}

	DBfree_result(result);
}

static void	get_target_host_main_data(zbx_uint64_t hostid, zbx_vector_str_t *templates_triggers_descriptions,
		zbx_hashset_t *zbx_host_triggers_main_data, zbx_vector_uint64_t *temp_host_triggerids,
		zbx_hashset_t *triggers_descriptions)
{
	char		*sql = NULL;
	size_t		sql_alloc = 256, sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;

	zbx_vector_uint64_create(temp_host_triggerids);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
		"select distinct t.triggerid,t.description,t.expression,t.recovery_expression"
			",t.flags,t.recovery_mode,t.correlation_mode,t.manual_close,t.opdata,t.discover,t.event_name"
		" from triggers t,functions f,items i"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and t.templateid is null"
				" and i.hostid=" ZBX_FS_UI64
				" and", hostid);

	DBadd_str_condition_alloc(&sql, &sql_alloc, &sql_offset, "t.description",
			(const char **)templates_triggers_descriptions->values,
			templates_triggers_descriptions->values_num);

	result = DBselect("%s", sql);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_target_host_trigger_entry_t		target_host_trigger_entry;
		zbx_trigger_descriptions_entry_t	*found, temp_t;

		target_host_trigger_entry.update_flags = 0;
		ZBX_STR2UINT64(target_host_trigger_entry.triggerid,row[0]);
		target_host_trigger_entry.description = zbx_strdup(NULL, row[1]);
		target_host_trigger_entry.expression = zbx_strdup(NULL, row[2]);
		target_host_trigger_entry.recovery_expression = zbx_strdup(NULL, row[3]);
		ZBX_STR2UINT64(target_host_trigger_entry.flags_orig, zbx_strdup(NULL, row[4]));
		ZBX_STR2UCHAR(target_host_trigger_entry.recovery_mode_orig, zbx_strdup(NULL, row[5]));
		ZBX_STR2UCHAR(target_host_trigger_entry.correlation_mode_orig, zbx_strdup(NULL, row[6]));
		ZBX_STR2UCHAR(target_host_trigger_entry.manual_close_orig, zbx_strdup(NULL, row[7]));
		target_host_trigger_entry.opdata_orig = zbx_strdup(NULL, row[8]);
		ZBX_STR2UCHAR(target_host_trigger_entry.discover_orig, zbx_strdup(NULL, row[9]));
		target_host_trigger_entry.event_name_orig = zbx_strdup(NULL, row[10]);
		zbx_hashset_insert(zbx_host_triggers_main_data, &target_host_trigger_entry,
				sizeof(target_host_trigger_entry));
		zbx_vector_uint64_append(temp_host_triggerids, target_host_trigger_entry.triggerid);

		temp_t.description = zbx_strdup(NULL, target_host_trigger_entry.description);

		if (NULL != (found = (zbx_trigger_descriptions_entry_t *)zbx_hashset_search(
				triggers_descriptions, &temp_t)))
		{
			zbx_vector_uint64_append(&(found->triggerids), target_host_trigger_entry.triggerid);
		}
		else
		{
			zbx_vector_uint64_t			triggerids_local;
			zbx_trigger_descriptions_entry_t	local_temp_t;

			zbx_vector_uint64_create(&triggerids_local);
			zbx_vector_uint64_append(&triggerids_local, target_host_trigger_entry.triggerid);
			zbx_vector_uint64_create(&(local_temp_t.triggerids));

			local_temp_t.description = zbx_strdup(NULL, target_host_trigger_entry.description);
			zbx_vector_uint64_append(&(local_temp_t.triggerids), target_host_trigger_entry.triggerid);
			zbx_hashset_insert(triggers_descriptions, &local_temp_t, sizeof(local_temp_t));
		}
	}

	zbx_free(sql);
	DBfree_result(result);
}

static int	compare_triggers(zbx_trigger_copy_t * template_trigger, zbx_target_host_trigger_entry_t *main_found,
		zbx_hashset_t *zbx_templates_triggers_funcs, zbx_hashset_t *zbx_host_triggers_funcs)
{
	int	i, j, ret = FAIL;
	char	*expr, *rexpr, *old_expr;
	char	search[MAX_ID_LEN + 3], replace[MAX_ID_LEN + 3];

	zbx_trigger_functions_entry_t	*found_template_trigger_funcs, temp_t_template_trigger_funcs,
			*found_host_trigger_funcs, temp_t_host_trigger_funcs;

	zabbix_log(LOG_LEVEL_INFORMATION, "In %s()", __func__);

	expr = zbx_strdup(NULL, main_found->expression);
	rexpr = zbx_strdup(NULL, main_found->recovery_expression);

	temp_t_template_trigger_funcs.triggerid = template_trigger->triggerid;
	temp_t_host_trigger_funcs.triggerid = main_found->triggerid;

	if (NULL != (found_template_trigger_funcs = (zbx_trigger_functions_entry_t *)zbx_hashset_search(
			zbx_templates_triggers_funcs, &temp_t_template_trigger_funcs)) &&
			NULL != (found_host_trigger_funcs = (zbx_trigger_functions_entry_t *)
			zbx_hashset_search(zbx_host_triggers_funcs,
			&temp_t_host_trigger_funcs)))
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers one of funcs is NOT NULL, functionids values: %d",
				found_template_trigger_funcs->functionids.values_num);

		zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers one of funcs is NOT NULL, itemkeys: %d",
				found_template_trigger_funcs->itemkeys.values_num);

		zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers one of funcs is NOT NULL, parameters: %d",
				found_template_trigger_funcs->parameters.values_num);

		for (i = 0; i < found_template_trigger_funcs->functionids.values_num; i++)
		{

			zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers, found template funcs itemkeys: %s",
					found_template_trigger_funcs->itemkeys.values[i]);

			for (j = 0; j < found_host_trigger_funcs->functionids.values_num;
					j++)
			{
				zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers, found functionsids: %s",
						found_host_trigger_funcs->functionids.values[j]);

				zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers, found itemkeys: %s",
						found_host_trigger_funcs->itemkeys.values[j]);

				zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers, found parameters: %s",
						found_host_trigger_funcs->itemkeys.values[j]);

				if (0 == strcmp(found_template_trigger_funcs->itemkeys.values[i],
						found_host_trigger_funcs->itemkeys.values[j]) &&
						0 == strcmp(found_template_trigger_funcs->parameters.values[i],
						found_host_trigger_funcs->parameters.values[j]))
				{
					/* replace */
					zbx_snprintf(search, sizeof(search), "{%s}",
							found_host_trigger_funcs->functionids.values[j]);
					zbx_snprintf(replace, sizeof(replace), "{%s}",
							found_template_trigger_funcs->functionids.values[i]);

					zabbix_log(LOG_LEVEL_INFORMATION, "compare_trggers BEFORE expr: %s, rexpr: %s",
							expr, rexpr);

					old_expr = expr;
					expr = string_replace(expr, search, replace);
					zbx_free(old_expr);

					old_expr = rexpr;
					rexpr = string_replace(rexpr, search, replace);
					zbx_free(old_expr);
					zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers AFTER expr: %s, rexpr: %s",
							expr, rexpr);
				}
			}
		}
	}

	zabbix_log(LOG_LEVEL_INFORMATION, "compare_triggers template_trigger_expression: ->%s<-, expr: ->%s<-",
			template_trigger->expression, expr);

	if (0 != strcmp(template_trigger->expression, expr) ||
		0 != strcmp(template_trigger->recovery_expression, rexpr))
	{
		goto out;
	}
	ret = SUCCEED;
out:
	zbx_free(rexpr);
	zbx_free(expr);

	zabbix_log(LOG_LEVEL_INFORMATION, "End of %s():%s", __func__, zbx_result_string(ret));

	return ret;
}

static int	mark_updates_for_host_trigger(zbx_trigger_copy_t *trigger_copy,
		zbx_target_host_trigger_entry_t *main_found)
{
	int	res = FAIL;

	zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER");

	if (trigger_copy->flags != main_found->flags_orig)
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER flags");

		main_found->flags = trigger_copy->flags;
		main_found->update_flags |= ZBX_FLAG_LINK_FUNCTION_UPDATE_FLAGS;
		res = SUCCEED;
	}

	if (trigger_copy->recovery_mode != main_found->recovery_mode_orig)
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER recovery_mode");

		main_found->recovery_mode = trigger_copy->recovery_mode;
		main_found->update_flags |= ZBX_FLAG_LINK_FUNCTION_UPDATE_RECOVERY_MODE;
		res = SUCCEED;
	}

	if (trigger_copy->correlation_mode != main_found->correlation_mode_orig)
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER correlation_mode");

		main_found->correlation_mode = trigger_copy->correlation_mode;
		main_found->update_flags |= ZBX_FLAG_LINK_FUNCTION_UPDATE_CORRELATION_MODE;
		res = SUCCEED;
	}

	if (trigger_copy->manual_close != main_found->manual_close_orig)
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER manual_close");

		main_found->manual_close = trigger_copy->manual_close;
		main_found->update_flags |= ZBX_FLAG_LINK_FUNCTION_UPDATE_MANUAL_CLOSE;
		res = SUCCEED;
	}

	if (0 != strcmp(trigger_copy->opdata, main_found->opdata_orig))
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER trigger_copy");

		main_found->opdata = zbx_strdup(NULL, trigger_copy->opdata);
		main_found->update_flags |= ZBX_FLAG_LINK_FUNCTION_UPDATE_OPDATA;
		res = SUCCEED;
	}

	if (trigger_copy->discover != main_found->discover_orig)
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER discover");

		main_found->discover = trigger_copy->discover;
		main_found->update_flags |= ZBX_FLAG_LINK_FUNCTION_UPDATE_DISCOVER;
		res = SUCCEED;
	}

	if (0 != strcmp(trigger_copy->event_name, main_found->event_name_orig))
	{
		zabbix_log(LOG_LEVEL_INFORMATION, "UPDATING TRIGGER event_name");

		main_found->event_name = zbx_strdup(NULL, trigger_copy->event_name);
		main_found->update_flags |= ZBX_FLAG_LINK_FUNCTION_UPDATE_EVENT_NAME;
		res = SUCCEED;
	}

	return res;
}

static void	execute_triggers_updates(zbx_hashset_t *zbx_host_triggers_main_data)
{
	const char	*d;
	char		*sql = NULL;
	size_t		sql_alloc = 512, sql_offset = 0;

	zbx_hashset_iter_t		iter1;
	zbx_target_host_trigger_entry_t	*fo;

	zbx_hashset_iter_reset(zbx_host_triggers_main_data, &iter1);
	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	while (NULL != (fo = (zbx_target_host_trigger_entry_t *)zbx_hashset_iter_next(&iter1)))
	{
		d = "";

		if (0 != (fo->update_flags & ZBX_FLAG_LINK_TRIGGER_UPDATE))
		{
			zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset, "update triggers set ");

			if (0 != (fo->update_flags & ZBX_FLAG_LINK_FUNCTION_UPDATE_FLAGS))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "flags=%d", (int)fo->flags);
				d = ",";
			}

			if (0 != (fo->update_flags & ZBX_FLAG_LINK_FUNCTION_UPDATE_RECOVERY_MODE))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%srecovery_mode=%d", d,
						fo->recovery_mode);
				d = ",";
			}

			if (0 != (fo->update_flags & ZBX_FLAG_LINK_FUNCTION_UPDATE_CORRELATION_MODE))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%scorrelation_mode=%d", d,
						fo->correlation_mode);
				d = ",";
			}

			if (0 != (fo->update_flags & ZBX_FLAG_LINK_FUNCTION_UPDATE_MANUAL_CLOSE))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%smanual_close=%d", d,
						fo->manual_close);
				d = ",";
			}

			if (0 != (fo->update_flags & ZBX_FLAG_LINK_FUNCTION_UPDATE_OPDATA))
			{
				char	*opdata_esc = DBdyn_escape_string(fo->opdata);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sopdata='%s'", d, opdata_esc);
				zbx_free(opdata_esc);
				d = ",";
			}

			if (0 != (fo->update_flags & ZBX_FLAG_LINK_FUNCTION_UPDATE_DISCOVER))
			{
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sdiscover=%d", d, fo->discover);
				d = ",";
			}

			if (0 != (fo->update_flags & ZBX_FLAG_LINK_FUNCTION_UPDATE_EVENT_NAME))
			{
				char	*event_name_esc = DBdyn_escape_string(fo->event_name);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "%sevent_name='%s'", d,
						fo->event_name);
				zbx_free(event_name_esc);
				d = ",";
			}
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where triggerid=" ZBX_FS_UI64 ";\n",
					fo->triggerid);
		}
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (16 < sql_offset)
		DBexecute("%s", sql);

	zbx_free(sql);
}

static void	get_funcs_for_insert(zbx_uint64_t hostid, zbx_vector_uint64_t *insert_templateid_triggerids,
		zbx_hashset_t *zbx_insert_triggers_funcs)
{
	int		res = SUCCEED;
	char		*sql = NULL;
	size_t		sql_alloc = 512, sql_offset = 0;
	DB_RESULT	result;
	DB_ROW		row;
	zbx_uint64_t	itemid;

	if (0 == insert_templateid_triggerids->values_num)
		return;

	zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
			" select hi.itemid,tf.functionid,tf.name,tf.parameter,ti.key_,tf.triggerid"
			" from functions tf,items ti"
			" left join items hi"
				" on hi.key_=ti.key_"
					" and hi.hostid=" ZBX_FS_UI64
				" where tf.itemid=ti.itemid"
					" and",
			hostid);
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "tf.triggerid", insert_templateid_triggerids->values,
			insert_templateid_triggerids->values_num);

	result = DBselect("%s", sql);

	while (SUCCEED == res && NULL != (row = DBfetch(result)))
	{
		zbx_trigger_functions_entry_t	*found, temp_t;

		if (SUCCEED != DBis_null(row[0]))
		{
			int i;

			ZBX_STR2UINT64(itemid, row[0]);
			ZBX_STR2UINT64(temp_t.triggerid, row[5]);

			if (NULL != (found =  (zbx_trigger_functions_entry_t *)zbx_hashset_search(
					zbx_insert_triggers_funcs, &temp_t)))
			{
				zbx_vector_uint64_append(&(found->itemids), itemid);
				zbx_vector_str_append(&(found->functionids), zbx_strdup(NULL, row[1]));
				zbx_vector_str_append(&(found->itemkeys), zbx_strdup(NULL, row[4]));
				/* zbx_vector_str_append(&(found->functionids),  row[1]); */
				/* zbx_vector_str_append(&(found->itemkeys), row[4]); */
				zbx_vector_str_append(&(found->names), DBdyn_escape_string(row[2]));
				zbx_vector_str_append(&(found->parameters), DBdyn_escape_string(row[3]));

				for (i = 0; i < (found->names).values_num; i++)
					zabbix_log(LOG_LEVEL_INFORMATION, "BADGER STRA0: ->%s<-",(found->names).values[i]);
			}
			else
			{
				zbx_vector_uint64_t		itemids_local;
				zbx_trigger_functions_entry_t	local_temp_t;
				zbx_vector_str_t		functionids_local,itemkeys_local, names_local,
								parameters_local;

				zbx_vector_str_create(&functionids_local);
				zbx_vector_uint64_create(&itemids_local);
				zbx_vector_str_create(&itemkeys_local);
				zbx_vector_str_create(&names_local);
				zbx_vector_str_create(&parameters_local);

				zbx_vector_uint64_append(&itemids_local, itemid);
				zbx_vector_str_append(&functionids_local, zbx_strdup(NULL, row[1]));
				zbx_vector_str_append(&itemkeys_local, zbx_strdup(NULL, row[4]));
				zbx_vector_str_append(&names_local, DBdyn_escape_string(row[2]));
				zbx_vector_str_append(&parameters_local, DBdyn_escape_string(row[3]));

				/*local_temp_t = (zbx_trigger_functions_entry_t *)zbx_malloc(NULL,
					sizeof(zbx_trigger_functions_entry_t));*/
				local_temp_t.triggerid = temp_t.triggerid;
				local_temp_t.functionids = functionids_local;
				local_temp_t.itemids = itemids_local;
				local_temp_t.itemkeys = itemkeys_local;
				local_temp_t.names = names_local;

				local_temp_t.parameters = parameters_local;
				zbx_hashset_insert(zbx_insert_triggers_funcs, &local_temp_t, sizeof(local_temp_t));

				for (i = 0; i < local_temp_t.names.values_num; i++)
					zabbix_log(LOG_LEVEL_INFORMATION, "BADGER STRA1: ->%s<-",
					local_temp_t.names.values[i]);
			}
		}
		else
		{
			res = FAIL;
		}

	}

	zbx_free(sql);
	DBfree_result(result);
}

static int	execute_triggers_inserts(zbx_vector_trigger_copies_insert_t *trigger_copies_insert,
		zbx_hashset_t *zbx_insert_triggers_funcs, zbx_vector_uint64_t *new_triggerids, char **error)
{
	int	res, i, j;
	char	*sql_update_triggers_expr = NULL;
	size_t	sql_update_triggers_expr_alloc = 512, sql_update_triggers_expr_offset = 0;

	zbx_uint64_t	triggerid, functionid;
	zbx_db_insert_t	db_insert, db_insert_funcs;

	zbx_trigger_functions_entry_t	*found, temp_t;

	DBbegin_multiple_update(&sql_update_triggers_expr, &sql_update_triggers_expr_alloc,
			&sql_update_triggers_expr_offset);

	zbx_db_insert_prepare(&db_insert, "triggers", "triggerid", "description", "priority", "status", "comments",
			"url", "type", "value", "state", "templateid", "flags", "recovery_mode", "correlation_mode",
			"correlation_tag", "manual_close", "opdata", "discover", "event_name", NULL);

	zbx_db_insert_prepare(&db_insert_funcs, "functions", "functionid", "itemid", "triggerid", "name",
			"parameter", NULL);

	triggerid = DBget_maxid_num("triggers", trigger_copies_insert->values_num);
	functionid = DBget_maxid_num("functions", zbx_insert_triggers_funcs->num_data);

	for (i = 0; i < trigger_copies_insert->values_num; i++)
	{
		zbx_eval_context_t	ctx, ctx_r;
		zbx_trigger_copy_t	*trigger_copy_template = trigger_copies_insert->values[i];

		zbx_db_insert_add_values(&db_insert, triggerid, trigger_copy_template->description,
				(int)trigger_copy_template->priority, (int)trigger_copy_template->status,
				trigger_copy_template->comments, trigger_copy_template->url,
				(int)trigger_copy_template->type, (int)TRIGGER_VALUE_OK, (int)TRIGGER_STATE_NORMAL,
				trigger_copy_template->templateid, (int)trigger_copy_template->flags,
				(int)trigger_copy_template->recovery_mode, (int)trigger_copy_template->correlation_mode,
				trigger_copy_template->correlation_tag, (int)trigger_copy_template->manual_close,
				trigger_copy_template->opdata, trigger_copy_template->discover,
				trigger_copy_template->event_name);

		zbx_vector_uint64_append(new_triggerids, triggerid);

		if (SUCCEED != (res = zbx_eval_parse_expression(&ctx, trigger_copy_template->expression,
				ZBX_EVAL_PARSE_TRIGGER_EXPRESSSION | ZBX_EVAL_COMPOSE_FUNCTIONID, error)))
		{
			goto func_out;
		}

		if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == (int)trigger_copy_template->recovery_mode &&
				(SUCCEED != (res = zbx_eval_parse_expression(&ctx_r,
				trigger_copy_template->recovery_expression,
				ZBX_EVAL_PARSE_TRIGGER_EXPRESSSION | ZBX_EVAL_COMPOSE_FUNCTIONID,
				error))))
		{
			zbx_eval_clear(&ctx);
			goto func_out;
		}

		temp_t.triggerid = trigger_copy_template->templateid;

		if (NULL != (found =  (zbx_trigger_functions_entry_t *)zbx_hashset_search(zbx_insert_triggers_funcs,
				&temp_t)))
		{
			for (j = 0; j < found->functionids.values_num; j++)
			{
				zbx_uint64_t old_functionid;

				ZBX_DBROW2UINT64(old_functionid, found->functionids.values[j]);
				zbx_eval_replace_functionid(&ctx, old_functionid, functionid);
				if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION ==
						(int)trigger_copy_template->recovery_mode)
				{
					zbx_eval_replace_functionid(&ctx_r, old_functionid, functionid);
				}

				zabbix_log(LOG_LEVEL_INFORMATION, "BADGER: triggerid: %lu", triggerid);
				zabbix_log(LOG_LEVEL_INFORMATION, "BADGER: names: %s", found->names.values[j]);
				zabbix_log(LOG_LEVEL_INFORMATION, "BADGER: parameters: %s", found->parameters.values[j]);
				zabbix_log(LOG_LEVEL_INFORMATION, "BADGER: values: %lu", found->itemids.values[j]);

				zbx_db_insert_add_values(&db_insert_funcs, functionid++,
						found->itemids.values[j], triggerid, found->names.values[j],
						found->parameters.values[j]);
			}
		}

		if (SUCCEED == (res = zbx_eval_validate_replaced_functionids(&ctx, error)) &&
				TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == (int)trigger_copy_template->recovery_mode)
		{
			res = zbx_eval_validate_replaced_functionids(&ctx_r, error);
		}

		if (SUCCEED == res)
		{
			char	*new_expression = NULL, *esc;

			zbx_eval_compose_expression(&ctx, &new_expression);
			esc = DBdyn_escape_field("triggers", "expression", new_expression);
			zbx_snprintf_alloc(&sql_update_triggers_expr, &sql_update_triggers_expr_alloc,
					&sql_update_triggers_expr_offset,
					"update triggers set expression='%s'", esc);
			zbx_free(esc);
			zbx_free(new_expression);

			if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == (int)trigger_copy_template->recovery_mode)
			{
					zbx_eval_compose_expression(&ctx_r, &new_expression);
					esc = DBdyn_escape_field("triggers", "recovery_expression", new_expression);
					zbx_snprintf_alloc(&sql_update_triggers_expr,
							&sql_update_triggers_expr_alloc, &sql_update_triggers_expr_offset,
							",recovery_expression='%s'", esc);
					zbx_free(esc);
					zbx_free(new_expression);
			}

			zbx_snprintf_alloc(&sql_update_triggers_expr, &sql_update_triggers_expr_alloc,
					&sql_update_triggers_expr_offset,
					" where triggerid=" ZBX_FS_UI64 ";\n",
					triggerid);
		}

		zbx_eval_clear(&ctx);
		if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == (int)trigger_copy_template->recovery_mode)
		{
			zbx_eval_clear(&ctx_r);
		}
func_out:
	triggerid++;
	}

	zbx_db_insert_execute(&db_insert);
	zbx_db_insert_clean(&db_insert);

	zbx_db_insert_execute(&db_insert_funcs);
	zbx_db_insert_clean(&db_insert_funcs);

	DBend_multiple_update(&sql_update_triggers_expr, &sql_update_triggers_expr_alloc,
			&sql_update_triggers_expr_offset);

	if (sql_update_triggers_expr_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql_update_triggers_expr);
	zbx_free(sql_update_triggers_expr);

	return res;
}

static void	process_triggers(zbx_trigger_copy_t *trigger_copy_template, zbx_hashset_t *host_triggers_descriptions,
		zbx_hashset_t *zbx_host_triggers_main_data, zbx_hashset_t *zbx_templates_triggers_funcs,
		zbx_hashset_t *zbx_host_triggers_funcs, int *upd_triggers, int *ins_triggers,
		zbx_vector_uint64_t *cur_triggerids, zbx_vector_trigger_copies_insert_t *trigger_copies_insert,
		zbx_vector_uint64_t *insert_templateid_triggerids)
{
	int					j, found_descriptions_match = FAIL;
	zbx_trigger_descriptions_entry_t	*found, temp_t;

	temp_t.description = trigger_copy_template->description;

	if (NULL != (found =  (zbx_trigger_descriptions_entry_t *)zbx_hashset_search(
			host_triggers_descriptions, &temp_t)))
	{
		for (j = 0; j < found->triggerids.values_num; j++)
		{
			zbx_target_host_trigger_entry_t	main_temp_t, *main_found;

			zabbix_log(LOG_LEVEL_INFORMATION, "NEXT ii found: %lu", found->triggerids.values[j]);
			main_temp_t.triggerid = found->triggerids.values[j];

			if (NULL != (main_found =  (zbx_target_host_trigger_entry_t *)zbx_hashset_search(
					zbx_host_triggers_main_data, &main_temp_t)) &&
					SUCCEED == compare_triggers(trigger_copy_template, main_found,
					zbx_templates_triggers_funcs, zbx_host_triggers_funcs))
			{
				zabbix_log(LOG_LEVEL_INFORMATION, "TRIGGERS %lu and %lu",
						trigger_copy_template->triggerid,
						found->triggerids.values[j]);
				found_descriptions_match = SUCCEED;

				if (SUCCEED == mark_updates_for_host_trigger(trigger_copy_template, main_found))
				{
					(*upd_triggers)++;
					zbx_vector_uint64_append(cur_triggerids, found->triggerids.values[j]);
				}

				break;
			}
		}
	}

	/* not found any entries with descriptions, insert */

	if (FAIL == found_descriptions_match)
	{
		zbx_trigger_copy_t	*trigger_copy_insert;

		/* save data for trigger */
		zabbix_log(LOG_LEVEL_INFORMATION, "INSERTING TRIGGER, SAVE DATA");
		(*ins_triggers)++;

		trigger_copy_insert = (zbx_trigger_copy_t *)zbx_malloc(NULL, sizeof(zbx_trigger_copy_t));
		trigger_copy_insert->description = zbx_strdup(NULL, trigger_copy_template->description);
		trigger_copy_insert->priority = trigger_copy_template->priority;
		trigger_copy_insert->status = trigger_copy_template->status;
		trigger_copy_insert->comments =  DBdyn_escape_string(trigger_copy_template->comments);
		trigger_copy_insert->url = DBdyn_escape_string(trigger_copy_template->url);
		trigger_copy_insert->type = trigger_copy_template->type;

		trigger_copy_insert->templateid = trigger_copy_template->triggerid;
		zabbix_log(LOG_LEVEL_INFORMATION, "KRONEG ALPHA: %u", trigger_copy_template->flags);
		trigger_copy_insert->flags = trigger_copy_template->flags;
		trigger_copy_insert->recovery_mode = trigger_copy_template->recovery_mode;
		trigger_copy_insert->correlation_mode = trigger_copy_template->correlation_mode;
		trigger_copy_insert->correlation_tag = zbx_strdup(NULL, trigger_copy_template->correlation_tag);
		trigger_copy_insert->manual_close = trigger_copy_template->manual_close;
		trigger_copy_insert->opdata = zbx_strdup(NULL, trigger_copy_template->opdata);
		trigger_copy_insert->discover = trigger_copy_template->discover;
		trigger_copy_insert->event_name = zbx_strdup(NULL, trigger_copy_template->event_name);

		trigger_copy_insert->expression= zbx_strdup(NULL, trigger_copy_template->expression);
		trigger_copy_insert->recovery_expression= zbx_strdup(NULL,
				trigger_copy_template->recovery_expression);

		zbx_vector_trigger_copies_insert_append(trigger_copies_insert, trigger_copy_insert);

		zbx_vector_uint64_append(insert_templateid_triggerids, trigger_copy_template->triggerid);
	}
}

static int	DBcmp_triggers(zbx_uint64_t triggerid1, const char *expression1, const char *recovery_expression1,
		zbx_uint64_t triggerid2, const char *expression2, const char *recovery_expression2)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		search[MAX_ID_LEN + 3], replace[MAX_ID_LEN + 3], *old_expr = NULL, *expr = NULL, *rexpr = NULL;
	int		res = SUCCEED;

	expr = zbx_strdup(NULL, expression2);
	rexpr = zbx_strdup(NULL, recovery_expression2);

	result = DBselect(
			"select f1.functionid,f2.functionid"
			" from functions f1,functions f2,items i1,items i2"
			" where f1.name=f2.name"
				" and f1.parameter=f2.parameter"
				" and i1.key_=i2.key_"
				" and i1.itemid=f1.itemid"
				" and i2.itemid=f2.itemid"
				" and f1.triggerid=" ZBX_FS_UI64
				" and f2.triggerid=" ZBX_FS_UI64,
				triggerid1, triggerid2);

	while (NULL != (row = DBfetch(result)))
	{
		zbx_snprintf(search, sizeof(search), "{%s}", row[1]);
		zbx_snprintf(replace, sizeof(replace), "{%s}", row[0]);

		old_expr = expr;
		expr = string_replace(expr, search, replace);
		zbx_free(old_expr);

		old_expr = rexpr;
		rexpr = string_replace(rexpr, search, replace);
		zbx_free(old_expr);
	}
	DBfree_result(result);

	if (0 != strcmp(expression1, expr) || 0 != strcmp(recovery_expression1, rexpr))
		res = FAIL;

	zbx_free(rexpr);
	zbx_free(expr);

	return res;
}

static int	DBcopy_trigger_to_host(zbx_uint64_t *new_triggerid, zbx_uint64_t *cur_triggerid, zbx_uint64_t hostid,
		zbx_uint64_t triggerid, const char *description, const char *expression,
		const char *recovery_expression, unsigned char recovery_mode, unsigned char status, unsigned char type,
		unsigned char priority, const char *comments, const char *url, unsigned char flags,
		unsigned char correlation_mode, const char *correlation_tag, unsigned char manual_close,
		const char *opdata, unsigned char discover, const char *event_name, char **error)
{
	DB_RESULT	result;
	DB_ROW		row;
	char		*sql = NULL;
	size_t		sql_alloc = 256, sql_offset = 0;
	zbx_uint64_t	itemid,	h_triggerid, functionid;
	char		*description_esc = NULL,
			*comments_esc = NULL,
			*url_esc = NULL,
			*function_esc = NULL,
			*parameter_esc = NULL,
			*correlation_tag_esc,
			*opdata_esc, *event_name_esc;
	int		res = FAIL;

	sql = (char *)zbx_malloc(sql, sql_alloc);

	DBbegin_multiple_update(&sql, &sql_alloc, &sql_offset);

	description_esc = DBdyn_escape_string(description);
	correlation_tag_esc = DBdyn_escape_string(correlation_tag);
	opdata_esc = DBdyn_escape_string(opdata);
	event_name_esc = DBdyn_escape_string(event_name);

	result = DBselect(
			"select distinct t.triggerid,t.expression,t.recovery_expression"
			" from triggers t,functions f,items i"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and t.templateid is null"
				" and i.hostid=" ZBX_FS_UI64
				" and t.description='%s'",
			hostid, description_esc);

	while (NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(h_triggerid, row[0]);

		if (SUCCEED != DBcmp_triggers(triggerid, expression, recovery_expression,
				h_triggerid, row[1], row[2]))
			continue;

		/* link not linked trigger with same description and expression */
		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"update triggers"
				" set templateid=" ZBX_FS_UI64
					",flags=%d"
					",recovery_mode=%d"
					",correlation_mode=%d"
					",correlation_tag='%s'"
					",manual_close=%d"
					",opdata='%s'"
					",discover=%d"
					",event_name='%s'"
				" where triggerid=" ZBX_FS_UI64 ";\n",
				triggerid, (int)flags, (int)recovery_mode, (int)correlation_mode, correlation_tag_esc,
				(int)manual_close, opdata_esc, (int)discover, event_name_esc, h_triggerid);

		*new_triggerid = 0;
		*cur_triggerid = h_triggerid;

		res = SUCCEED;
		break;
	}
	DBfree_result(result);

	/* create trigger if no updated triggers */
	if (SUCCEED != res)
	{
		zbx_eval_context_t	ctx, ctx_r;

		*new_triggerid = DBget_maxid("triggers");
		*cur_triggerid = 0;

		comments_esc = DBdyn_escape_string(comments);
		url_esc = DBdyn_escape_string(url);

		zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
				"insert into triggers"
					" (triggerid,description,priority,status,"
						"comments,url,type,value,state,templateid,flags,recovery_mode,"
						"correlation_mode,correlation_tag,manual_close,opdata,discover,"
						"event_name)"
					" values (" ZBX_FS_UI64 ",'%s',%d,%d,"
						"'%s','%s',%d,%d,%d," ZBX_FS_UI64 ",%d,%d,"
						"%d,'%s',%d,'%s',%d,'%s');\n",
					*new_triggerid, description_esc, (int)priority, (int)status, comments_esc,
					url_esc, (int)type, TRIGGER_VALUE_OK, TRIGGER_STATE_NORMAL, triggerid,
					(int)flags, (int)recovery_mode, (int)correlation_mode, correlation_tag_esc,
					(int)manual_close, opdata_esc, (int)discover, event_name_esc);

		zbx_free(url_esc);
		zbx_free(comments_esc);

		if (SUCCEED != (res = zbx_eval_parse_expression(&ctx, expression,
				ZBX_EVAL_PARSE_TRIGGER_EXPRESSSION | ZBX_EVAL_COMPOSE_FUNCTIONID, error)))
			goto out;

		if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == recovery_mode &&
				(SUCCEED != (res = zbx_eval_parse_expression(&ctx_r, recovery_expression,
						ZBX_EVAL_PARSE_TRIGGER_EXPRESSSION | ZBX_EVAL_COMPOSE_FUNCTIONID,
						error))))
		{
			zbx_eval_clear(&ctx);
			goto out;
		}

		/* Loop: functions */
		result = DBselect(
				"select hi.itemid,tf.functionid,tf.name,tf.parameter,ti.key_"
				" from functions tf,items ti"
				" left join items hi"
					" on hi.key_=ti.key_"
						" and hi.hostid=" ZBX_FS_UI64
				" where tf.itemid=ti.itemid"
					" and tf.triggerid=" ZBX_FS_UI64,
				hostid, triggerid);

		while (SUCCEED == res && NULL != (row = DBfetch(result)))
		{
			if (SUCCEED != DBis_null(row[0]))
			{
				zbx_uint64_t	old_functionid;

				ZBX_STR2UINT64(itemid, row[0]);

				functionid = DBget_maxid("functions");

				function_esc = DBdyn_escape_string(row[2]);
				parameter_esc = DBdyn_escape_string(row[3]);

				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset,
						"insert into functions"
						" (functionid,itemid,triggerid,name,parameter)"
						" values (" ZBX_FS_UI64 "," ZBX_FS_UI64 ","
							ZBX_FS_UI64 ",'%s','%s');\n",
						functionid, itemid, *new_triggerid,
						function_esc, parameter_esc);

				ZBX_DBROW2UINT64(old_functionid, row[1]);
				zbx_eval_replace_functionid(&ctx, old_functionid, functionid);
				if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == recovery_mode)
					zbx_eval_replace_functionid(&ctx_r, old_functionid, functionid);

				zbx_free(parameter_esc);
				zbx_free(function_esc);
			}
			else
			{
				*error = zbx_dsprintf(*error, "Missing similar key '%s' for host [" ZBX_FS_UI64 "]",
						row[4], hostid);
				res = FAIL;
			}
		}
		DBfree_result(result);

		if (SUCCEED == (res = zbx_eval_validate_replaced_functionids(&ctx, error)) &&
				TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == recovery_mode)
		{
			res = zbx_eval_validate_replaced_functionids(&ctx_r, error);
		}

		if (SUCCEED == res)
		{
			char	*new_expression = NULL, *esc;

			zbx_eval_compose_expression(&ctx, &new_expression);
			esc = DBdyn_escape_field("triggers", "expression", new_expression);
			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, "update triggers set expression='%s'", esc);
			zbx_free(esc);
			zbx_free(new_expression);

			if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == recovery_mode)
			{
				zbx_eval_compose_expression(&ctx_r, &new_expression);
				esc = DBdyn_escape_field("triggers", "recovery_expression", new_expression);
				zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, ",recovery_expression='%s'", esc);
				zbx_free(esc);
				zbx_free(new_expression);
			}

			zbx_snprintf_alloc(&sql, &sql_alloc, &sql_offset, " where triggerid=" ZBX_FS_UI64 ";\n",
					*new_triggerid);
		}

		zbx_eval_clear(&ctx);
		if (TRIGGER_RECOVERY_MODE_RECOVERY_EXPRESSION == recovery_mode)
			zbx_eval_clear(&ctx_r);
	}

	DBend_multiple_update(&sql, &sql_alloc, &sql_offset);

	if (sql_offset > 16)	/* In ORACLE always present begin..end; */
		DBexecute("%s", sql);
out:
	zbx_free(sql);
	zbx_free(correlation_tag_esc);
	zbx_free(event_name_esc);
	zbx_free(opdata_esc);
	zbx_free(description_esc);

	return res;
}

int	DBcopy_template_triggers2(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids, char **error)
{
	char			*sql = NULL;
	size_t			sql_alloc = 512, sql_offset = 0;
	DB_RESULT		result;
	DB_ROW			row;
	zbx_uint64_t		triggerid, new_triggerid, cur_triggerid;
	int			res = SUCCEED;
	zbx_vector_uint64_t	new_triggerids, cur_triggerids;

	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&new_triggerids);
	zbx_vector_uint64_create(&cur_triggerids);

	sql = (char *)zbx_malloc(sql, sql_alloc);

	sql_offset = 0;
	zbx_strcpy_alloc(&sql, &sql_alloc, &sql_offset,
			"select distinct t.triggerid,t.description,t.expression,t.status,"
				"t.type,t.priority,t.comments,t.url,t.flags,t.recovery_expression,t.recovery_mode,"
				"t.correlation_mode,t.correlation_tag,t.manual_close,t.opdata,t.discover,t.event_name"
			" from triggers t,functions f,items i"
			" where t.triggerid=f.triggerid"
				" and f.itemid=i.itemid"
				" and");
	DBadd_condition_alloc(&sql, &sql_alloc, &sql_offset, "i.hostid", templateids->values, templateids->values_num);

	result = DBselect("%s", sql);

	zbx_free(sql);

	while (SUCCEED == res && NULL != (row = DBfetch(result)))
	{
		ZBX_STR2UINT64(triggerid, row[0]);

		res = DBcopy_trigger_to_host(&new_triggerid, &cur_triggerid, hostid, triggerid,
				row[1],				/* description */
				row[2],				/* expression */
				row[9],				/* recovery_expression */
				(unsigned char)atoi(row[10]),	/* recovery_mode */
				(unsigned char)atoi(row[3]),	/* status */
				(unsigned char)atoi(row[4]),	/* type */
				(unsigned char)atoi(row[5]),	/* priority */
				row[6],				/* comments */
				row[7],				/* url */
				(unsigned char)atoi(row[8]),	/* flags */
				(unsigned char)atoi(row[11]),	/* correlation_mode */
				row[12],			/* correlation_tag */
				(unsigned char)atoi(row[13]),	/* manual_close */
				row[14],			/* opdata */
				(unsigned char)atoi(row[15]),	/* discover */
				row[16],			/* event_name */
				error);

		if (0 != new_triggerid)				/* new trigger added */
			zbx_vector_uint64_append(&new_triggerids, new_triggerid);
		else
			zbx_vector_uint64_append(&cur_triggerids, cur_triggerid);
	}
	DBfree_result(result);

	if (SUCCEED == res)
	{
		res = DBadd_template_dependencies_for_new_triggers(hostid, new_triggerids.values,
				new_triggerids.values_num);
	}

	if (SUCCEED == res)
		res = DBcopy_template_trigger_tags(&new_triggerids, &cur_triggerids);

	zbx_vector_uint64_destroy(&cur_triggerids);
	zbx_vector_uint64_destroy(&new_triggerids);

	if (FAIL == res && NULL == *error)
		*error = zbx_strdup(NULL, "unknown error while linking triggers");

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(res));

	return res;
}

static void trigger_copies_free(zbx_trigger_copy_t *trigger_copy)
{
	zbx_free(trigger_copy->comments);
	zbx_free(trigger_copy->url);
	zbx_free(trigger_copy->expression);
	zbx_free(trigger_copy->recovery_expression);
	zbx_free(trigger_copy->event_name);
	zbx_free(trigger_copy->correlation_tag);
	zbx_free(trigger_copy->description);
	zbx_free(trigger_copy->opdata);
	zbx_free(trigger_copy);
}

/******************************************************************************
 *                                                                            *
 * Function: DBcopy_template_triggers                                         *
 *                                                                            *
 * Purpose: Copy template triggers to host                                    *
 *                                                                            *
 * Parameters: hostid      - [IN] host identificator from database            *
 *             templateids - [IN] array of template IDs                       *
 *             error       - [IN] the error message                           *
 *                                                                            *
 * Return value: upon successful completion return SUCCEED                    *
 *                                                                            *
 * Author: Eugene Grigorjev                                                   *
 *                                                                            *
 * Comments: !!! Don't forget to sync the code with PHP !!!                   *
 *                                                                            *
 ******************************************************************************/
int	DBcopy_template_triggers(zbx_uint64_t hostid, const zbx_vector_uint64_t *templateids, char **error)
{
	int					i, upd_triggers = 0, ins_triggers = 0, res = SUCCEED;
	zbx_vector_uint64_t			new_triggerids, cur_triggerids;
	zbx_vector_trigger_copies_templates_t	trigger_copies_templates;
	zbx_vector_trigger_copies_insert_t	trigger_copies_insert;
	zbx_vector_str_t			templates_triggers_descriptions;
	zbx_vector_uint64_t			temp_templates_triggerids, temp_host_triggerids,
						insert_templateid_triggerids;
	zbx_hashset_t				zbx_templates_triggers_funcs, zbx_host_triggers_funcs,
						zbx_host_triggers_main_data, host_triggers_descriptions,
						zbx_insert_triggers_funcs;


	zabbix_log(LOG_LEVEL_DEBUG, "In %s()", __func__);

	zbx_vector_uint64_create(&new_triggerids);
	zbx_vector_uint64_create(&cur_triggerids);
	zbx_vector_uint64_create(&insert_templateid_triggerids);
	zbx_vector_uint64_create(&temp_templates_triggerids);
	zbx_vector_str_create(&templates_triggers_descriptions);

	zbx_vector_trigger_copies_insert_create(&trigger_copies_insert);
	zbx_vector_trigger_copies_templates_create(&trigger_copies_templates);

#define	TRIGGER_FUNCS_HASHSET_DEF_SIZE	100
	zbx_hashset_create(&host_triggers_descriptions, TRIGGER_FUNCS_HASHSET_DEF_SIZE,
			zbx_triggers_descriptions_hash_func,
			zbx_triggers_descriptions_compare_func);
	zbx_hashset_create(&zbx_templates_triggers_funcs, TRIGGER_FUNCS_HASHSET_DEF_SIZE,
			zbx_triggers_functions_hash_func,
			zbx_triggers_functions_compare_func);
	zbx_hashset_create(&zbx_host_triggers_funcs, TRIGGER_FUNCS_HASHSET_DEF_SIZE,
			zbx_triggers_functions_hash_func,
			zbx_triggers_functions_compare_func);
	zbx_hashset_create(&zbx_host_triggers_main_data, TRIGGER_FUNCS_HASHSET_DEF_SIZE,
			zbx_host_triggers_main_data_hash_func,
			zbx_host_triggers_main_data_compare_func);
	zbx_hashset_create(&zbx_insert_triggers_funcs, TRIGGER_FUNCS_HASHSET_DEF_SIZE,
			zbx_triggers_functions_hash_func,
			zbx_triggers_functions_compare_func);
#undef TRIGGER_FUNCS_HASHSET_DEF_SIZE

	get_templates_triggers_data(hostid, templateids, &trigger_copies_templates, &templates_triggers_descriptions,
			&temp_templates_triggerids, &res);

	if (0 == templates_triggers_descriptions.values_num)
	{
		goto end;
	}

	get_target_host_main_data(hostid, &templates_triggers_descriptions, &zbx_host_triggers_main_data,
			&temp_host_triggerids, &host_triggers_descriptions);
	get_trigger_funcs(&temp_templates_triggerids, &zbx_templates_triggers_funcs);
	get_trigger_funcs(&temp_host_triggerids, &zbx_host_triggers_funcs);


	{
		zbx_hashset_iter_t iter1, iter2;
		zbx_trigger_functions_entry_t *fo;

		zbx_hashset_iter_reset(&zbx_templates_triggers_funcs, &iter1);
		zbx_hashset_iter_reset(&zbx_host_triggers_funcs, &iter2);

		while (NULL != (fo = (zbx_trigger_functions_entry_t*)zbx_hashset_iter_next(&iter1)))
			zabbix_log(LOG_LEVEL_INFORMATION, "DIAMON1: %lu", fo->triggerid);

		while (NULL != (fo =  (zbx_trigger_functions_entry_t*)zbx_hashset_iter_next(&iter2)))
			zabbix_log(LOG_LEVEL_INFORMATION, "DIAMON2: %lu", fo->triggerid);
	}

	for (i = 0; i < trigger_copies_templates.values_num; i++)
	{
		process_triggers(trigger_copies_templates.values[i], &host_triggers_descriptions,
				&zbx_host_triggers_main_data, &zbx_templates_triggers_funcs, &zbx_host_triggers_funcs,
				&upd_triggers, &ins_triggers, &cur_triggerids, &trigger_copies_insert,
				&insert_templateid_triggerids);
	}

	get_funcs_for_insert(hostid, &insert_templateid_triggerids, &zbx_insert_triggers_funcs);

	zabbix_log(LOG_LEVEL_INFORMATION, "UPD TRIGGERS: %d", upd_triggers);
	if (0 < upd_triggers)
		execute_triggers_updates(&zbx_host_triggers_main_data);

	if (0 < ins_triggers)
		execute_triggers_inserts(&trigger_copies_insert, &zbx_insert_triggers_funcs, &new_triggerids, error);

	if (SUCCEED == res)
	{
		res = DBadd_template_dependencies_for_new_triggers(hostid, new_triggerids.values,
				new_triggerids.values_num);
	}

	if (SUCCEED == res)
		res = DBcopy_template_trigger_tags(&new_triggerids, &cur_triggerids);


	if (FAIL == res && NULL == *error)
		*error = zbx_strdup(NULL, "unknown error while linking triggers");
end:
	zbx_vector_uint64_destroy(&new_triggerids);
	zbx_vector_uint64_destroy(&cur_triggerids);
	zbx_vector_uint64_destroy(&insert_templateid_triggerids);
	zbx_vector_uint64_destroy(&temp_templates_triggerids);
	zbx_vector_str_destroy(&templates_triggers_descriptions);

	zbx_vector_trigger_copies_templates_clear_ext(&trigger_copies_templates, trigger_copies_free);
	zbx_vector_trigger_copies_templates_destroy(&trigger_copies_templates);
	zbx_vector_trigger_copies_insert_clear_ext(&trigger_copies_insert, trigger_copies_free);
	zbx_vector_trigger_copies_insert_destroy(&trigger_copies_insert);

	zbx_hashset_destroy(&host_triggers_descriptions);
	zbx_hashset_destroy(&zbx_host_triggers_main_data);
	zbx_hashset_destroy(&zbx_insert_triggers_funcs);

	zbx_triggers_functions_clean(&zbx_templates_triggers_funcs);
	zbx_triggers_functions_clean(&zbx_host_triggers_funcs);

	zabbix_log(LOG_LEVEL_DEBUG, "End of %s():%s", __func__, zbx_result_string(res));

	return res;
}

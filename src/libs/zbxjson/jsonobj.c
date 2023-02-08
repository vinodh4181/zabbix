/*
** Zabbix
** Copyright (C) 2001-2023 Zabbix SIA
**
** This program is free software; you can redistribute it and/or modify
** it under the terms of the GNU General Public License as published by
** the Free Software Foundation; either version 2 of the License, or
** (at your option) any later version.
**
** This program is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
** GNU General Public License for more details.
**
** You should have received a copy of the GNU General Public License
** along with this program; if not, write to the Free Software
** Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
**/

#include "jsonobj.h"

#include "json_parser.h"
#include "json.h"

ZBX_PTR_VECTOR_IMPL(jsonobj_ptr, zbx_jsonobj_t *)
ZBX_VECTOR_IMPL(jsonobj_ref, zbx_jsonobj_ref_t)

/* jsonobject index hashset support */

static zbx_hash_t	jsonobj_index_el_hash(const void *v)
{
	const zbx_jsonobj_index_el_t	*el = (const zbx_jsonobj_index_el_t *)v;

	return ZBX_DEFAULT_STRING_HASH_FUNC(el->value);
}

static int	jsonobj_index_el_compare(const void *v1, const void *v2)
{
	const zbx_jsonobj_index_el_t	*el1 = (const zbx_jsonobj_index_el_t *)v1;
	const zbx_jsonobj_index_el_t	*el2 = (const zbx_jsonobj_index_el_t *)v2;

	return strcmp(el1->value, el2->value);
}

/* jsonobject values hashset support */

static zbx_hash_t	jsonobj_el_hash(const void *v)
{
	const zbx_jsonobj_el_t	*el = (const zbx_jsonobj_el_t *)v;

	return ZBX_DEFAULT_STRING_HASH_FUNC(el->name);
}

static int	jsonobj_el_compare(const void *v1, const void *v2)
{
	const zbx_jsonobj_el_t	*el1 = (const zbx_jsonobj_el_t *)v1;
	const zbx_jsonobj_el_t	*el2 = (const zbx_jsonobj_el_t *)v2;

	return strcmp(el1->name, el2->name);
}

/******************************************************************************
 *                                                                            *
 * Purpose: initialize json object structure                                  *
 *                                                                            *
 * Parameters: obj  - [IN/OUT] the json object to initialize                  *
 *             type - [IN] the json object type                               *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_init(zbx_jsonobj_t *obj, zbx_json_type_t type)
{
	obj->type = type;

	switch (type)
	{
		case ZBX_JSON_TYPE_ARRAY:
			zbx_vector_jsonobj_ptr_create(&obj->data.array);
			break;
		case ZBX_JSON_TYPE_OBJECT:
			zbx_hashset_create(&obj->data.object, 0, jsonobj_el_hash, jsonobj_el_compare);
			break;
		default:
			memset(&obj->data, 0, sizeof(obj->data));
			break;
	}

	obj->index = NULL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: free resources allocated by json object index element             *
 *                                                                            *
 * Parameters: v  - [IN] the json index element                               *
 *                                                                            *
 ******************************************************************************/
static void	jsonobj_index_el_clear(void *v)
{
	zbx_jsonobj_index_el_t	*el = (zbx_jsonobj_index_el_t *)v;
	int			i;

	zbx_free(el->value);
	for (i = 0; i < el->objects.values_num; i++)
	{
		zbx_free(el->objects.values[i].name);

		if (0 != el->objects.values[i].external)
		{
			zbx_jsonobj_clear(el->objects.values[i].value);
			zbx_free(el->objects.values[i].value);
		}
	}

	zbx_vector_jsonobj_ref_destroy(&el->objects);
}

/******************************************************************************
 *                                                                            *
 * Purpose: initialize json object index                                      *
 *                                                                            *
 * Parameters: obj  - [IN/OUT] the json object                                *
 *             path - [IN] the indexed relative path                          *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_init_index(zbx_jsonobj_t *obj, const char *path)
{
	obj->index = (zbx_jsonobj_index_t *)zbx_malloc(NULL, sizeof(zbx_jsonobj_index_t));
	obj->index->path = zbx_strdup(NULL, path);
	zbx_hashset_create_ext(&obj->index->objects, 0, jsonobj_index_el_hash, jsonobj_index_el_compare,
			jsonobj_index_el_clear, ZBX_DEFAULT_MEM_MALLOC_FUNC, ZBX_DEFAULT_MEM_REALLOC_FUNC,
			ZBX_DEFAULT_MEM_FREE_FUNC);
}

/******************************************************************************
 *                                                                            *
 * Purpose: set string value to json object                                   *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_set_string(zbx_jsonobj_t *obj, char *str)
{
	obj->type = ZBX_JSON_TYPE_STRING;
	obj->data.string = str;
}

/******************************************************************************
 *                                                                            *
 * Purpose: set numeric value to json object                                  *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_set_number(zbx_jsonobj_t *obj, double number)
{
	obj->type = ZBX_JSON_TYPE_NUMBER;
	obj->data.number = number;
}

/******************************************************************************
 *                                                                            *
 * Purpose: set true value to json object                                     *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_set_true(zbx_jsonobj_t *obj)
{
	obj->type = ZBX_JSON_TYPE_TRUE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: set false value to json object                                    *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_set_false(zbx_jsonobj_t *obj)
{
	obj->type = ZBX_JSON_TYPE_FALSE;
}

/******************************************************************************
 *                                                                            *
 * Purpose: set null value to json object                                     *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_set_null(zbx_jsonobj_t *obj)
{
	obj->type = ZBX_JSON_TYPE_NULL;
}

/******************************************************************************
 *                                                                            *
 * Purpose: initialize json object element                                    *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_el_init(zbx_jsonobj_el_t *el)
{
	el->name = NULL;
	jsonobj_init(&el->value, ZBX_JSON_TYPE_UNKNOWN);
}

/******************************************************************************
 *                                                                            *
 * Purpose: free resources allocated by json object element                   *
 *                                                                            *
 ******************************************************************************/
void	jsonobj_el_clear(zbx_jsonobj_el_t *el)
{
	zbx_free(el->name);
	zbx_jsonobj_clear(&el->value);
}

/******************************************************************************
 *                                                                            *
 * Purpose: free json object                                                  *
 *                                                                            *
 ******************************************************************************/
static void	jsonobj_free(zbx_jsonobj_t *obj)
{
	zbx_jsonobj_clear(obj);
	zbx_free(obj);
}

/******************************************************************************
 *                                                                            *
 * Purpose: free resources allocated by json object                           *
 *                                                                            *
 ******************************************************************************/
void	zbx_jsonobj_clear(zbx_jsonobj_t *obj)
{
	zbx_jsonobj_el_t	*el;
	zbx_hashset_iter_t	iter;

	switch (obj->type)
	{
		case ZBX_JSON_TYPE_STRING:
			zbx_free(obj->data.string);
			break;
		case ZBX_JSON_TYPE_ARRAY:
			zbx_vector_jsonobj_ptr_clear_ext(&obj->data.array, jsonobj_free);
			zbx_vector_jsonobj_ptr_destroy(&obj->data.array);
			break;
		case ZBX_JSON_TYPE_OBJECT:
			zbx_hashset_iter_reset(&obj->data.object, &iter);
			while (NULL != (el = (zbx_jsonobj_el_t *)zbx_hashset_iter_next(&iter)))
			{
				zbx_free(el->name);
				zbx_jsonobj_clear(&el->value);
			}
			zbx_hashset_destroy(&obj->data.object);
			break;
		default:
			break;
	}

	if (NULL != obj->index)
	{
		zbx_free(obj->index->path);
		zbx_hashset_destroy(&obj->index->objects);
		zbx_free(obj->index);
	}
}

/******************************************************************************
 *                                                                            *
 * Purpose: convert json object to text format                                *
 *                                                                            *
 ******************************************************************************/
int	zbx_jsonobj_to_string(char **str, size_t *str_alloc, size_t *str_offset, zbx_jsonobj_t *obj)
{
	char			*tmp, buf[32];
	int			i;
	zbx_hashset_iter_t	iter;
	zbx_jsonobj_el_t	*el;

	switch (obj->type)
	{
		case ZBX_JSON_TYPE_TRUE:
			zbx_strcpy_alloc(str, str_alloc, str_offset, "true");
			break;
		case ZBX_JSON_TYPE_FALSE:
			zbx_strcpy_alloc(str, str_alloc, str_offset, "false");
			break;
		case ZBX_JSON_TYPE_NULL:
			zbx_strcpy_alloc(str, str_alloc, str_offset, "null");
			break;
		case ZBX_JSON_TYPE_STRING:
			tmp = zbx_strdup(NULL, obj->data.string);
			zbx_json_escape(&tmp);
			zbx_snprintf_alloc(str, str_alloc, str_offset, "\"%s\"", tmp);
			zbx_free(tmp);
			break;
		case ZBX_JSON_TYPE_NUMBER:
			zbx_print_double(buf, sizeof(buf), obj->data.number);
			zbx_strcpy_alloc(str, str_alloc, str_offset, buf);
			break;
		case ZBX_JSON_TYPE_ARRAY:
			zbx_chrcpy_alloc(str, str_alloc, str_offset, '[');
			for (i = 0; i < obj->data.array.values_num; i++)
			{
				if (0 != i)
					zbx_chrcpy_alloc(str, str_alloc, str_offset, ',');

				zbx_jsonobj_to_string(str, str_alloc, str_offset, obj->data.array.values[i]);
			}
			zbx_chrcpy_alloc(str, str_alloc, str_offset, ']');
			break;
		case ZBX_JSON_TYPE_OBJECT:
			zbx_chrcpy_alloc(str, str_alloc, str_offset, '{');
			zbx_hashset_iter_reset(&obj->data.object, &iter);
			while (NULL != (el = (zbx_jsonobj_el_t *)zbx_hashset_iter_next(&iter)))
			{
				if ((*str)[*str_offset - 1] != '{')
					zbx_chrcpy_alloc(str, str_alloc, str_offset, ',');

				tmp = zbx_strdup(NULL, el->name);
				zbx_json_escape(&tmp);
				zbx_snprintf_alloc(str, str_alloc, str_offset, "\"%s\"", tmp);
				zbx_chrcpy_alloc(str, str_alloc, str_offset, ':');
				zbx_free(tmp);

				zbx_jsonobj_to_string(str, str_alloc, str_offset, &el->value);
			}
			zbx_chrcpy_alloc(str, str_alloc, str_offset, '}');
			break;
		default:
			zbx_set_json_strerror("unknown json object with type: %u", obj->type);
			return FAIL;
	}

	return SUCCEED;
}

/******************************************************************************
 *                                                                            *
 * Purpose: parses json formatted data into json object structure             *
 *                                                                            *
 ******************************************************************************/
int	zbx_jsonobj_open(const char *data, zbx_jsonobj_t *obj)
{
	int	ret = FAIL;
	char	*error = NULL;

	SKIP_WHITESPACE(data);

	switch (*data)
	{
		case '{':
			if (0 == json_parse_object(data, obj, &error))
				goto out;
			break;
		case '[':
			if (0 == json_parse_array(data, obj, &error))
				goto out;
			break;
		default:
			/* not json data, failing */
			jsonobj_init(obj, ZBX_JSON_TYPE_UNKNOWN);
			(void)json_error("invalid object format, expected opening character '{' or '['", data, &error);
			goto out;
	}

	ret = SUCCEED;
out:
	if (FAIL == ret)
	{
		zbx_jsonobj_clear(obj);
		zbx_set_json_strerror("%s", error);
		zbx_free(error);
	}

	return ret;
}

/*
** Zabbix
** Copyright (C) 2001-2022 Zabbix SIA
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

#ifndef ZABBIX_STATS_H
#define ZABBIX_STATS_H

#include "zbxcomms.h"
#include "zbxjson.h"

typedef void (*zbx_zabbix_stats_ext_get_func_t)(struct zbx_json *json, const zbx_config_comms_args_t *zbx_config_comms);

void	zbx_zabbix_stats_init(zbx_zabbix_stats_ext_get_func_t cb);
void	zbx_zabbix_stats_get(struct zbx_json *json, const zbx_config_comms_args_t *zbx_config_comms);

#endif
/*  Copyright (C) 2011 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef _KNOT_CONF_INCLUDES_H_
#define _KNOT_CONF_INCLUDES_H_

#include <stdbool.h>

struct conf_includes;
typedef struct conf_includes conf_includes_t;

conf_includes_t *conf_includes_init(int capacity);
void conf_includes_free(conf_includes_t *includes);

bool conf_includes_can_push(conf_includes_t *includes);
bool conf_includes_push(conf_includes_t *includes, const char *filename);
char *conf_includes_top(conf_includes_t *includes);
char *conf_includes_pop(conf_includes_t *includes);

#endif /* _KNOT_CONF_INCLUDES_H_ */

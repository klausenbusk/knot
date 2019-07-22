/*  Copyright (C) 2019 CZ.NIC, z.s.p.o. <knot-dns@labs.nic.cz>

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#pragma once

#include "knot/common/stats_common.h"


#define DUMP_STR(fd, level, name, ...) do { \
	fprintf(fd, "%-.*s"name": %s\n", level, "    ", ##__VA_ARGS__); \
	} while (0)
#define DUMP_CTR(fd, level, name, ...) do { \
	fprintf(fd, "%-.*s"name": %"PRIu64"\n", level, "    ", ##__VA_ARGS__); \
	} while (0)


void dump_to_yaml(FILE *fd, server_t *server);
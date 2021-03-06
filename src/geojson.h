/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 4; tab-width: 4 -*- */
/*
 * viking -- GPS Data and Topo Analyzer, Explorer, and Manager
 *
 * Copyright (c) 2014, Rob Norris <rw_norris@hotmail.com>
 * Copyright (c) 2021, Rob Norris <rw_norris@hotmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */
#ifndef _VIKING_GEOJSON_H
#define _VIKING_GEOJSON_H

#include "viktrwlayer.h"

G_BEGIN_DECLS

gboolean a_geojson_write_file ( VikTrwLayer *vtl, FILE *ff );

const gchar* a_geojson_program_export ( void );
const gchar* a_geojson_program_import ( void );

gchar* a_geojson_import_to_gpx ( const gchar *filename );

gboolean a_geojson_read_file_OSRM ( VikTrwLayer *vtl, const gchar *filename );

G_END_DECLS

#endif

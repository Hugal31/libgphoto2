/* -*- Mode: C; indent-tabs-mode: t; c-basic-offset: 8; tab-width: 8 -*- */
/* ccapi.c
 *
 * Copyright (c) 2006 Marcus Meissner <marcus@jet.franken.de>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301  USA
 */

#include "config.h"
#include <gphoto2/gphoto2-port-library.h>

#include <stdlib.h>
#include <stdio.h>
#ifdef HAVE_FCNTL_H
# include <fcntl.h>
#endif
#include <errno.h>
#ifdef HAVE_SYS_TIME_H
# include <sys/time.h>
#endif
#ifdef HAVE_SYS_PARAM_H
# include <sys/param.h>
#endif
#include <string.h>

#include <gphoto2/gphoto2-port.h>
#include <gphoto2/gphoto2-port-result.h>
#include <gphoto2/gphoto2-port-log.h>

#include "libgphoto2_port/i18n.h"

#define CHECK(result) {int r=(result); if (r<0) return (r);}

GPPortType
gp_port_library_type (void)
{
	return GP_PORT_CCAPI;
}

int
gp_port_library_list (GPPortInfoList *list)
{
    GPPortInfo info;

    gp_port_info_new (&info);
	gp_port_info_set_type (info, GP_PORT_CCAPI);
	gp_port_info_set_name (info, _("CCAPI Connection"));
	gp_port_info_set_path (info, "ccapi:");
	CHECK (gp_port_info_list_append (list, info));

	/* Generic matcher so you can pass any IP address */
	gp_port_info_new (&info);
	gp_port_info_set_type (info, GP_PORT_CCAPI);
	gp_port_info_set_name (info, "");
	gp_port_info_set_path (info, "^ccapi:");
	gp_port_info_list_append (list, info); /* do not check return */

    return GP_OK;
}

static int
gp_port_ccapi_open (GPPort *port)
{
	return GP_OK;
}

static int
gp_port_ccapi_close (GPPort *port)
{
	return GP_OK;
}

static int
gp_port_ccapi_update (GPPort *port)
{
	return GP_OK;
}

GPPortOperations *
gp_port_library_operations (void)
{
	GPPortOperations *ops;

	ops = calloc (1, sizeof (GPPortOperations));
	if (!ops)
		return NULL;

	ops->open   = gp_port_ccapi_open;
	ops->close  = gp_port_ccapi_close;
    ops->update = gp_port_ccapi_update;

    return ops;
}

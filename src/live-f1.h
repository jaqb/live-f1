/* live-f1
 *
 * Copyright © 2005 Scott James Remnant <scott@netsplit.com>.
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
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301 USA
 */

#ifndef LIVE_F1_H
#define LIVE_F1_H

#include <gettext.h>

#include "macros.h"


/* Make gettext a little friendlier */
#define _(_str) gettext (_str)
#define N_(_str) gettext_noop (_str)


SJR_BEGIN_EXTERN

/* Program's name */
const char *program_name;

int info (int irrelevance, const char *format, ...);

SJR_END_EXTERN

#endif /* LIVE_F1_H */

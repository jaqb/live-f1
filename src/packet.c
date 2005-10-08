/* live-f1
 *
 * packet.c - individual packet handling
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

#ifdef HAVE_CONFIG_H
# include <config.h>
#endif /* HAVE_CONFIG_H */


#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "live-f1.h"
#include "display.h"
#include "http.h"
#include "stream.h"
#include "packet.h"


/**
 * handle_car_packet:
 * @state: application state structure,
 * @packet: decoded packet structure.
 *
 * Handle the car-related packet.
 **/
void
handle_car_packet (CurrentState *state,
		   const Packet *packet)
{
	/* Check whether a new car joined the event; actually, this is
	 * because we never know in advance how many cars there are, and
	 * things like practice sessions can probably have more than the
	 * usual twenty.  (Or we might get another team in the future).
	 *
	 * Then increase the size of all the arrays and clear the board.
	 */
	if (packet->car > state->num_cars) {
		int i, j;

		state->car_position = realloc (state->car_position,
					       sizeof (int) * packet->car);
		state->car_info = realloc (state->car_info,
					   sizeof (CarAtom *) * packet->car);

		for (i = state->num_cars; i < packet->car; i++) {
			state->car_position[i] = 0;
			state->car_info[i] = malloc (sizeof (CarAtom)
						     * LAST_CAR_PACKET);
			for (j = 0; j < LAST_CAR_PACKET; j++)
				memset (&state->car_info[i][j], 0,
					sizeof (CarAtom));
		}

		state->num_cars = packet->car;
		clear_board (state);
	}

	switch ((CarPacketType) packet->type) {
		CarAtom *atom;
		int      i;

	case CAR_POSITION_UPDATE:
		/* Position Update:
		 * Data: new position.
		 *
		 * This is a non-atom packet that indicates that the
		 * race position of a car has changed.  They often seem
		 * to come in pairs, the first one with a zero position,
		 * and the next with the new position, but not always
		 * sadly.
		 */
		clear_car (state, packet->car);
		for (i = 0; i < state->num_cars; i++)
			if (state->car_position[i] == packet->data)
				state->car_position[i] = 0;

		state->car_position[packet->car - 1] = packet->data;
		if (packet->data)
			update_car (state, packet->car);
		return;
	case CAR_POSITION_HISTORY:
		/* Currently unhandled */
		return;
	default:
		/* Data Atom:
		 * Format: string.
		 * Data: colour.
		 *
		 * Each of these events updates a particular piece of data
		 * for the car, some (with no length) only update the colour
		 * of a field.
		 */
		atom = &state->car_info[packet->car - 1][packet->type];
		atom->data = packet->data;
		if (packet->len >= 0)
			strcpy (atom->text, (const char *) packet->payload);

		update_cell (state, packet->car, packet->type);
		break;
	}
}

/**
 * handle_system_packet:
 * @state: application state structure,
 * @packet: decoded packet structure.
 *
 * Handle the system packet.
 **/
void
handle_system_packet (CurrentState *state,
		      const Packet *packet)
{
	switch ((SystemPacketType) packet->type) {
		unsigned int number, i;

	case SYS_EVENT_ID:
		/* Event Start:
		 * Format: odd byte, then decimal.
		 * Data: type of event.
		 *
		 * Indicates the start of an event, we use this to set up
		 * the board properly and obtain the decryption key for
		 * the event.
		 */
		number = 0;
		for (i = 1; i < packet->len; i++) {
			number *= 10;
			number += packet->payload[i] - '0';
		}

		state->key = obtain_decryption_key (state->host, number,
						    state->cookie);
		state->event_no = number;
		state->event_type = packet->data;
		state->epoch_time = 0;
		state->remaining_time = 0;
		state->lap = 0;
		state->num_cars = 0;
		if (state->car_position) {
			free (state->car_position);
			state->car_position = NULL;
		}
		if (state->car_info) {
			free (state->car_info);
			state->car_info = NULL;
		}
		reset_decryption (state);

		clear_board (state);
		info (3, _("Begin new event #%d (type: %d)\n"),
		      state->event_no, state->event_type);
		break;
	case SYS_KEY_FRAME:
		/* Key Frame Marker:
		 * Format: little-endian integer.
		 *
		 * If we've not yet encountered a key frame, we need to
		 * load this to get up to date.  Otherwise we just set
		 * our counter and carry on
		 */
		number = 0;
		i = packet->len;
		while (i) {
			number <<= 8;
			number |= packet->payload[--i];
		}

		reset_decryption (state);
		if (! state->frame) {
			state->frame = number;
			obtain_key_frame (state->host, number, state);
			reset_decryption (state);
		} else {
			state->frame = number;
		}

		break;
	case SYS_WEATHER:
		/* Weather Information:
		 * Format: decimal or string.
		 * Data: field to be updated.
		 *
		 * Indicates a change in the weather; the data field
		 * indicates which piece of information to change, the
		 * payload always contains the printed value.  This can be
		 * combined with the SYS_TIMESTAMP packet to record the
		 * changing of the data over time.
		 */
		switch (packet->data) {
		case 0:
			/* Session time remaining.
			 * This is only sent once a minute in H:MM:SS format,
			 * we therefore parse it and use it to update our
			 * own record of the amount of time remaining in the
			 * session.
			 *
			 * It also appears to be sent with a -1 length to
			 * indicate the passing of the minute; we use the
			 * first one of these to indicate the start of a
			 * session.
			 */
			if (packet->len > 0) {
				unsigned int total;

				total = number = 0;
				for (i = 0; i < packet->len; i++) {
					if (packet->payload[i] == ':') {
						total *= 60;
						total += number;
						number = 0;
					} else {
						number *= 10;
						number += (packet->payload[i]
							   - '0');
					}
				}
				total *= 60;
				total += number;

				if (state->epoch_time)
					state->epoch_time = time (NULL);
				state->remaining_time = total;
			} else {
				state->epoch_time = time (NULL);
			}

			close_popup ();
			update_time (state);
			break;
		default:
			/* Unhandled field */
			break;
		}
		break;
	case SYS_TRACK_STATUS:
		/* Track Status:
		 * Format: decimal.
		 * Data: field to be updated.
		 *
		 * Indicates a change in the status of the track; the data
		 * field indicates which piece of information to change.
		 */
		switch (packet->data) {
		case 1:
			/* Flag currently in effect.
			 * Decimal enum value.
			 */
			state->flag = packet->payload[0] - '0';
			update_status (state);
			break;
		default:
			/* Unhandled field */
			break;
		}
		break;
	case SYS_COPYRIGHT:
		/* Copyright Notice:
		 * Format: string.
		 *
		 * Plain text copyright notice in the start of the feed.
		 */
		info (2, "%s\n", packet->payload);
		break;
	case SYS_NOTICE:
		/* Important System Notice:
		 * Format: string.
		 *
		 * Various important system notices get displayed this
		 * way.
		 */
		info (0, "%s\n", packet->payload);
		break;
	default:
		/* Unhandled event */
		break;
	}
}

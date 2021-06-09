/*
* Wire
* Copyright (C) 2016 Wire Swiss GmbH
*
* This program is free software: you can redistribute it and/or modify
* it under the terms of the GNU General Public License as published by
* the Free Software Foundation, either version 3 of the License, or
* (at your option) any later version.
*
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
* GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program. If not, see <http://www.gnu.org/licenses/>.
*/


typedef void (devpair_send_h)(const char *pairid,
			      const uint8_t *data, size_t len, void *arg);
typedef void (devpair_estab_h)(const char *pairid, void *arg);
typedef void (devpair_data_h)(const char *pairid,
			      const uint8_t *data, size_t len, void *arg);
typedef void (devpair_xfer_h)(int err, const char *pairid, void *arg);
typedef void (devpair_close_h)(int err, const char *pairid, void *arg);

typedef void (devpair_file_rcv_h)(const char *pairid, const char *location, void *arg);
typedef void (devpair_file_snd_h)(const char *pairid, const char *name, bool success, void *arg);


/* Initialize device pairing module */
int devpair_init(void);
/* Close device parining module */
void devpair_close(void);


/* Create a device pairing.
 *
 * Typically device pairing is createed by the 'new' "un-provisioned" device.
 * The data parameter will contain the SDP-offer,
 * TURN credentials and username, from the 'old' pairing device.
 * To accept the device pairing, the user needs to call the devpair_accept
 * function.
 *
 * Returns the username for the device pairing, or NULL on error.
 */

const char *devpair_create(const char *pairid,
			   const uint8_t *data, size_t len);


/* Accept a device pairing.
 *
 * Typically device pairing is accepted by the new device, on the pairing id
 * that the pairing was created on. It will result in sending the SDP-answer.
 *
 * When a usable data connection is established the estab_handler
 * will be called, and the devpair_xfer function may be used.
 * If the data connection is closed for any reason,
 * the close_handler will be called to indicate this.
 *
 *
 */
int devpair_accept(const char *pairid,
		   devpair_send_h *sendh,
		   devpair_estab_h *estabh,
		   devpair_data_h *datah,
		   devpair_close_h *closeh,
		   void *arg);

/* Publish a pending device pairing attempt.
 *
 * Typically device pairing is published by the 'old' device, when it gets the
 * pairing id from the new device. This results in the sending of the
 * TURN credentials, SDP offer amd username for this pairing attempt.
 */
int devpair_publish(const char *pairid,
		    const char *username,
		    devpair_send_h *sendh,
		    void *arg);

/* Acknowledge a device pairing attempt.
 * Typically this will be called on the 'old' device when the SDP-answer
 * (contained in the data paramerter) is received from the 'new' device.
 *
 * When a usable data connection is established the estab_handler
 * will be called, and the devpair_xfer function may be used.
 * If the data connection is closed for any reason,
 * the close_handler will be called to indicate this.
 */
int devpair_ack(const char *pairid,
		const uint8_t *data, size_t len,
		devpair_estab_h *estabh,
		devpair_data_h *datah,
		devpair_close_h *closeh);


/* Response of sending data.
 * The err argument should be used to indicate an error.
 * Error value of 0 means success.
 *
 * The arg argument needs to be that as sent to the send_handler
 * for this request
 */
void devpair_resp(int err, const char *pairid, void *arg);

/* Transfer data on an established device pair.
 *
 * This can only be sucessfull after the stab_handler has been called for
 * a pairing attempt, and before the close_handler is called.
 */
int devpair_xfer(const char *pairid,
		 const uint8_t *data, size_t len,
		 devpair_xfer_h *xferh, void *arg);

int devpair_register_ft_handlers(const char *pairid,
		 const char *rcv_path,
		 devpair_file_rcv_h *f_rcv_h,
		 devpair_file_snd_h *f_snd_h);

int devpair_xfer_file(const char *pairid,
		 const char *file,
		 const char *name,
		 int speed_kbps);

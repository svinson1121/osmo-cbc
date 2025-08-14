/* SMSCB FSM: Represent master state about one SMSCB message. Parent of smscb_peer_fsm */

/* (C) 2019-2020 by Harald Welte <laforge@gnumonks.org>
 * All Rights Reserved
 *
 * SPDX-License-Identifier: AGPL-3.0+
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Affero General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Affero General Public License for more details.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 */

#include <osmocom/core/utils.h>
#include <osmocom/core/linuxlist.h>
#include <osmocom/core/talloc.h>
#include <osmocom/core/fsm.h>

#include <osmocom/gsm/gsm23003.h>
#include <osmocom/gsm/protocol/gsm_08_08.h>
#include <osmocom/gsm/gsm0808_utils.h>
#include <osmocom/gsm/cbsp.h>

#include <osmocom/cbc/cbc_data.h>
#include <osmocom/cbc/cbsp_link.h>
#include <osmocom/cbc/debug.h>
#include <osmocom/cbc/rest_it_op.h>
#include <osmocom/cbc/smscb_message_fsm.h>
#include <osmocom/cbc/smscb_peer_fsm.h>

#define S(x)    (1 << (x))

const struct value_string smscb_message_fsm_event_names[] = {
	{ SMSCB_MSG_E_CHILD_DIED,	"CHILD_DIED" },
	{ SMSCB_MSG_E_CREATE,		"CREATE" },
	{ SMSCB_MSG_E_REPLACE,		"REPLACE" },
	{ SMSCB_MSG_E_STATUS,		"STATUS" },
	{ SMSCB_MSG_E_DELETE,		"DELETE" },
	{ SMSCB_MSG_E_WRITE_ACK,	"WRITE_ACK" },
	{ SMSCB_MSG_E_WRITE_NACK,	"WRITE_NACK" },
	{ SMSCB_MSG_E_REPLACE_ACK,	"REPLACE_ACK" },
	{ SMSCB_MSG_E_REPLACE_NACK,	"REPLACE_NACK" },
	{ SMSCB_MSG_E_DELETE_ACK,	"DELETE_ACK" },
	{ SMSCB_MSG_E_DELETE_NACK,	"DELETE_NACK" },
	{ SMSCB_MSG_E_STATUS_ACK,	"STATUS_ACK" },
	{ SMSCB_MSG_E_STATUS_NACK,	"STATUS_NACK" },
	{ 0, NULL }
};

static void smscb_fsm_init(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct cbc_message *cbcmsg = fi->priv;

	switch (event) {
	case SMSCB_MSG_E_CREATE:
		OSMO_ASSERT(!cbcmsg->it_op);
		cbcmsg->it_op = data;
		osmo_fsm_inst_state_chg(fi, SMSCB_S_WAIT_WRITE_ACK, 15, T_WAIT_WRITE_ACK);
		/* forward this event to all child FSMs (i.e. all smscb_message_peer) */
		osmo_fsm_inst_broadcast_children(fi, SMSCB_PEER_E_CREATE, NULL);
		break;
	default:
		OSMO_ASSERT(0);
	}
}


static void smscb_fsm_wait_write_ack(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct cbc_message *cbcmsg = fi->priv;
	struct osmo_fsm_inst *peer_fi;

	switch (event) {
	case SMSCB_MSG_E_WRITE_ACK:
	case SMSCB_MSG_E_WRITE_NACK:
		/* check if any per-peer children have not yet received the ACK or
		 * timed out */
		llist_for_each_entry(peer_fi, &fi->proc.children, proc.child) {
			if (peer_fi->state != SMSCB_S_ACTIVE)
				return;
		}
		rest_it_op_set_http_result(cbcmsg->it_op, 201, "Created"); // FIXME: error cases
		osmo_fsm_inst_state_chg(fi, SMSCB_S_ACTIVE, 0, 0);
		if (cbcmsg->warning_period_sec != 0xffffffff) {
		osmo_timer_schedule(&fi->timer, cbcmsg->warning_period_sec, 0);
		fi->T = T_ACTIVE_EXPIRY;
		LOGP(DSMSCB, LOGL_INFO,
     		"Starting active expiry timer for message_id %u: %us",
     		cbcmsg->msg.message_id, cbcmsg->warning_period_sec);
		}
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void smscb_fsm_wait_write_ack_onleave(struct osmo_fsm_inst *fi, uint32_t new_state)
{
	struct cbc_message *cbcmsg = fi->priv;
	/* release the mutex from the REST interface + respond to user */
	rest_it_op_complete(cbcmsg->it_op);
	cbcmsg->it_op = NULL;
}

static void smscb_fsm_active(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct cbc_message *cbcmsg = fi->priv;

	switch (event) {
	case SMSCB_MSG_E_REPLACE:
		OSMO_ASSERT(!cbcmsg->it_op);
		cbcmsg->it_op = data;
		osmo_fsm_inst_state_chg(fi, SMSCB_S_WAIT_REPLACE_ACK, 15, T_WAIT_REPLACE_ACK);
		/* forward this event to all child FSMs (i.e. all smscb_message_peer) */
		osmo_fsm_inst_broadcast_children(fi, SMSCB_PEER_E_REPLACE, data);
		break;
	case SMSCB_MSG_E_STATUS:
		OSMO_ASSERT(!cbcmsg->it_op);
		cbcmsg->it_op = data;
		osmo_fsm_inst_state_chg(fi, SMSCB_S_WAIT_STATUS_ACK, 15, T_WAIT_STATUS_ACK);
		/* forward this event to all child FSMs (i.e. all smscb_message_peer) */
		osmo_fsm_inst_broadcast_children(fi, SMSCB_PEER_E_STATUS, data);
		break;
	case SMSCB_MSG_E_DELETE:
		OSMO_ASSERT(!cbcmsg->it_op);
		cbcmsg->it_op = data;
		osmo_fsm_inst_state_chg(fi, SMSCB_S_WAIT_DELETE_ACK, 15, T_WAIT_DELETE_ACK);
		/* forward this event to all child FSMs (i.e. all smscb_message_peer) */
		osmo_fsm_inst_broadcast_children(fi, SMSCB_PEER_E_DELETE, data);
		break;
	case SMSCB_MSG_E_EXPIRE:
        	LOGP(DSMSCB, LOGL_INFO,
             	"Message ID %u expired after %u seconds, moving to EXPIRED state",
             	cbcmsg->msg.message_id, cbcmsg->warning_period_sec);

        	// Move the message to the expired list
      		llist_del(&cbcmsg->list);
       		llist_add_tail(&cbcmsg->list, &g_cbc->expired_messages);
        	// Transition FSM state to EXPIRED
      		 osmo_fsm_inst_state_chg(fi, SMSCB_S_EXPIRED, 0, 0);
        	break;
	default:
		OSMO_ASSERT(0);
	}
}

static void smscb_fsm_wait_replace_ack(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct cbc_message *cbcmsg = fi->priv;
	struct osmo_fsm_inst *peer_fi;

	switch (event) {
	case SMSCB_MSG_E_REPLACE_ACK:
	case SMSCB_MSG_E_REPLACE_NACK:
		llist_for_each_entry(peer_fi, &fi->proc.children, proc.child) {
			if (peer_fi->state == SMSCB_S_WAIT_REPLACE_ACK)
				return;
		}
		rest_it_op_set_http_result(cbcmsg->it_op, 200, "OK"); // FIXME: error cases
		osmo_fsm_inst_state_chg(fi, SMSCB_S_ACTIVE, 0, 0);
		if (cbcmsg->warning_period_sec != 0xffffffff) {
		osmo_timer_schedule(&fi->timer, cbcmsg->warning_period_sec, 0);
		fi->T = T_ACTIVE_EXPIRY;
		LOGP(DSMSCB, LOGL_INFO,
  		"Starting active expiry timer for message_id %u: %us",
 		cbcmsg->msg.message_id, cbcmsg->warning_period_sec);
		}
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void smscb_fsm_wait_replace_ack_onleave(struct osmo_fsm_inst *fi, uint32_t new_state)
{
	struct cbc_message *cbcmsg = fi->priv;
	/* release the mutex from the REST interface + respond to user */
	rest_it_op_complete(cbcmsg->it_op);
	cbcmsg->it_op = NULL;
}

static void smscb_fsm_wait_status_ack(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct cbc_message *cbcmsg = fi->priv;
	struct osmo_fsm_inst *peer_fi;

	switch (event) {
	case SMSCB_MSG_E_STATUS_ACK:
	case SMSCB_MSG_E_STATUS_NACK:
		llist_for_each_entry(peer_fi, &fi->proc.children, proc.child) {
			if (peer_fi->state == SMSCB_S_WAIT_STATUS_ACK)
				return;
		}
		rest_it_op_set_http_result(cbcmsg->it_op, 200, "OK"); // FIXME: error cases
		osmo_fsm_inst_state_chg(fi, SMSCB_S_ACTIVE, 0, 0);
		if (cbcmsg->warning_period_sec != 0xffffffff) {
		osmo_timer_schedule(&fi->timer, cbcmsg->warning_period_sec, 0);
		fi->T = T_ACTIVE_EXPIRY;
		LOGP(DSMSCB, LOGL_INFO,
    		"Starting active expiry timer for message_id %u: %us",
   		cbcmsg->msg.message_id, cbcmsg->warning_period_sec);
		}
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void smscb_fsm_wait_status_ack_onleave(struct osmo_fsm_inst *fi, uint32_t new_state)
{
	struct cbc_message *cbcmsg = fi->priv;
	/* release the mutex from the REST interface + respond to user */
	rest_it_op_complete(cbcmsg->it_op);
	cbcmsg->it_op = NULL;
}

static void smscb_fsm_wait_delete_ack(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
	struct cbc_message *cbcmsg = fi->priv;
	struct osmo_fsm_inst *peer_fi;

	switch (event) {
	case SMSCB_MSG_E_DELETE_ACK:
	case SMSCB_MSG_E_DELETE_NACK:
		llist_for_each_entry(peer_fi, &fi->proc.children, proc.child) {
			if (peer_fi->state != SMSCB_S_DELETED)
				return;
		}
		rest_it_op_set_http_result(cbcmsg->it_op, 200, "OK"); // FIXME: error cases
		osmo_fsm_inst_state_chg(fi, SMSCB_S_DELETED, 0, 0);
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void smscb_fsm_wait_delete_ack_onleave(struct osmo_fsm_inst *fi, uint32_t new_state)
{
	struct cbc_message *cbcmsg = fi->priv;
	/* release the mutex from the REST interface + respond to user */
	if (cbcmsg->it_op) {
		rest_it_op_complete(cbcmsg->it_op);
		cbcmsg->it_op = NULL;
	}
}

static void smscb_fsm_deleted_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
	/* Stop the active expiry timer if it's running */
	osmo_timer_del(&fi->timer);

	/* release the mutex from the REST interface, then destroy */
	struct cbc_message *cbcmsg = fi->priv;
	if (cbcmsg->it_op) {
		rest_it_op_complete(cbcmsg->it_op);
		cbcmsg->it_op = NULL;
	}
	/* move from active to expired messages */
	llist_del(&cbcmsg->list);
	llist_add_tail(&cbcmsg->list, &g_cbc->expired_messages);
	cbcmsg->time.expired = time(NULL);
}


/*
static void smscb_fsm_active_onevent(struct osmo_fsm_inst *fi, uint32_t event, void *data)
{
    struct cbc_message *cbcmsg = fi->priv;

    switch (event) {
    case SMSCB_MSG_E_EXPIRE:
        LOGP(DSMSCB, LOGL_INFO, "Message ID %u moving to EXPIRED state", 
             cbcmsg->msg.message_id);

        llist_del(&cbcmsg->list);
        llist_add_tail(&cbcmsg->list, &g_cbc->expired_messages);

        osmo_fsm_inst_state_chg(fi, SMSCB_S_EXPIRED, 0, 0);
        break;

    default:
        OSMO_ASSERT(0);
    }
}
*/

static void smscb_fsm_expired_onenter(struct osmo_fsm_inst *fi, uint32_t old_state)
{
    struct cbc_message *cbcmsg = fi->priv;

    if (cbcmsg->it_op) {
        rest_it_op_complete(cbcmsg->it_op);
        cbcmsg->it_op = NULL;
    }

    LOGP(DSMSCB, LOGL_INFO, "Message ID %u is now EXPIRED", cbcmsg->msg.message_id);
}
	

static struct osmo_fsm_state smscb_fsm_states[] = {
	[SMSCB_S_INIT] = {
		.name = "INIT",
		.in_event_mask = S(SMSCB_MSG_E_CREATE),
		.out_state_mask = S(SMSCB_S_WAIT_WRITE_ACK),
		.action = smscb_fsm_init,
	},
	[SMSCB_S_WAIT_WRITE_ACK] = {
		.name = "WAIT_WRITE_ACK",
		.in_event_mask = S(SMSCB_MSG_E_WRITE_ACK) |
				 S(SMSCB_MSG_E_WRITE_NACK),
		.out_state_mask = S(SMSCB_S_ACTIVE),
		.action = smscb_fsm_wait_write_ack,
		.onleave = smscb_fsm_wait_write_ack_onleave,
	},
	[SMSCB_S_ACTIVE] = {
		.name = "ACTIVE",
		.in_event_mask = S(SMSCB_MSG_E_REPLACE) |
				 S(SMSCB_MSG_E_STATUS) |
				 S(SMSCB_MSG_E_DELETE) |
				  S(SMSCB_MSG_E_EXPIRE), 
		.out_state_mask = S(SMSCB_S_ACTIVE) |
				  S(SMSCB_S_WAIT_REPLACE_ACK) |
				  S(SMSCB_S_WAIT_STATUS_ACK) |
				  S(SMSCB_S_WAIT_DELETE_ACK) |
				  S(SMSCB_S_EXPIRED), 
		.action = smscb_fsm_active,
	},
	[SMSCB_S_WAIT_REPLACE_ACK] = {
		.name = "WAIT_REPLACE_ACK",
		.in_event_mask = S(SMSCB_MSG_E_REPLACE_ACK) |
				 S(SMSCB_MSG_E_REPLACE_NACK),
		.out_state_mask = S(SMSCB_S_ACTIVE),
		.action = smscb_fsm_wait_replace_ack,
		.onleave = smscb_fsm_wait_replace_ack_onleave,
	},
	[SMSCB_S_WAIT_STATUS_ACK] = {
		.name = "WAIT_STATUS_ACK",
		.in_event_mask = S(SMSCB_MSG_E_STATUS_ACK) |
				 S(SMSCB_MSG_E_STATUS_NACK),
		.out_state_mask = S(SMSCB_S_ACTIVE),
		.action = smscb_fsm_wait_status_ack,
		.onleave = smscb_fsm_wait_status_ack_onleave,
	},
	[SMSCB_S_WAIT_DELETE_ACK] = {
		.name = "WAIT_DELETE_ACK",
		.in_event_mask = S(SMSCB_MSG_E_DELETE_ACK) |
				 S(SMSCB_MSG_E_DELETE_NACK),
		.out_state_mask = S(SMSCB_S_DELETED),
		.action = smscb_fsm_wait_delete_ack,
		.onleave = smscb_fsm_wait_delete_ack_onleave,
	},
	[SMSCB_S_DELETED] = {
		.name = "DELETED",
		.in_event_mask = 0,
		.out_state_mask = 0,
		.onenter = smscb_fsm_deleted_onenter,
		//.action = smscb_fsm_deleted,
	},
	/* Add EXPIRED state to FSM */
	[SMSCB_S_EXPIRED] = {
   		 .onenter = smscb_fsm_expired_onenter,
	},

};

static int smscb_fsm_timer_cb(struct osmo_fsm_inst *fi)
{
	struct cbc_message *cbcmsg = fi->priv;

	switch (fi->T) {
	case T_WAIT_WRITE_ACK:
		/* onleave will take care of notifying the user */
		osmo_fsm_inst_state_chg(fi, SMSCB_S_ACTIVE, 0, 0);
		break;
	case T_WAIT_REPLACE_ACK:
		/* onleave will take care of notifying the user */
		osmo_fsm_inst_state_chg(fi, SMSCB_S_ACTIVE, 0, 0);
		break;
	case T_WAIT_STATUS_ACK:
		/* onleave will take care of notifying the user */
		osmo_fsm_inst_state_chg(fi, SMSCB_S_ACTIVE, 0, 0);
		break;
	case T_WAIT_DELETE_ACK:
		/* onleave will take care of notifying the user */
		osmo_fsm_inst_state_chg(fi, SMSCB_S_DELETED, 0, 0);
		break;
	case T_ACTIVE_EXPIRY:
		 LOGP(DSMSCB, LOGL_INFO, "Message ID %u expired after %u seconds",
           	  cbcmsg->msg.message_id, cbcmsg->warning_period_sec);
       		 osmo_fsm_inst_dispatch(fi, SMSCB_MSG_E_EXPIRE, NULL);
        	 break;

	default:
		OSMO_ASSERT(0);
	}
	return 0;
}

static void smscb_fsm_allstate(struct osmo_fsm_inst *Fi, uint32_t event, void *data)
{
	switch (event) {
	case SMSCB_MSG_E_CHILD_DIED:
		break;
	default:
		OSMO_ASSERT(0);
	}
}

static void smscb_fsm_cleanup(struct osmo_fsm_inst *fi, enum osmo_fsm_term_cause cause)
{
	struct cbc_message *cbcmsg = fi->priv;

	OSMO_ASSERT(llist_empty(&cbcmsg->peers));

	llist_del(&cbcmsg->list);
	/* memory of cbcmsg is child of fi and hence automatically free'd */
}

static struct osmo_fsm smscb_fsm = {
	.name = "SMSCB",
	.states = smscb_fsm_states,
	.num_states = ARRAY_SIZE(smscb_fsm_states),
	.allstate_event_mask = S(SMSCB_MSG_E_CHILD_DIED),
	.allstate_action = smscb_fsm_allstate,
	.timer_cb = smscb_fsm_timer_cb,
	.log_subsys = DSMSCB,
	.event_names = smscb_message_fsm_event_names,
	.cleanup= smscb_fsm_cleanup,
};


/* allocate a cbc_message, fill it with data from orig_msg, create FSM */
struct cbc_message *cbc_message_alloc(void *ctx, const struct cbc_message *orig_msg)
{
	struct cbc_message *smscb;
	struct osmo_fsm_inst *fi;
	char idbuf[32];

	if (cbc_message_by_id(orig_msg->msg.message_id)) {
		LOGP(DSMSCB, LOGL_ERROR, "Cannot create message_id %u (already exists)\n",
			orig_msg->msg.message_id);
		return NULL;
	}

	snprintf(idbuf, sizeof(idbuf), "%s-%u", orig_msg->cbe_name, orig_msg->msg.message_id);
	fi = osmo_fsm_inst_alloc(&smscb_fsm, ctx, NULL, LOGL_INFO, idbuf);
	if (!fi) {
		LOGP(DSMSCB, LOGL_ERROR, "Cannot allocate cbc_message fsm\n");
		return NULL;
	}

	smscb = talloc(fi, struct cbc_message);
	if (!smscb) {
		LOGP(DSMSCB, LOGL_ERROR, "Cannot allocate cbc_message\n");
		osmo_fsm_inst_term(fi, OSMO_FSM_TERM_ERROR, NULL);
		return NULL;
	}
	/* copy data from original message */
	memcpy(smscb, orig_msg, sizeof(*smscb));
	smscb->cbe_name  = talloc_strdup(smscb, orig_msg->cbe_name);
	/* initialize other members */
	INIT_LLIST_HEAD(&smscb->peers);
	smscb->fi = fi;
	smscb->it_op = NULL;
	smscb->time.created = time(NULL);

	fi->priv = smscb;

	/* add to global list of messages */
	llist_add_tail(&smscb->list, &g_cbc->messages);

	return smscb;
}

void cbc_message_free(struct cbc_message *cbcmsg)
{
	osmo_fsm_inst_term(cbcmsg->fi, OSMO_FSM_TERM_REGULAR, NULL);
}

__attribute__((constructor)) void smscb_fsm_constructor(void)
{
	OSMO_ASSERT(osmo_fsm_register(&smscb_fsm) == 0);
}

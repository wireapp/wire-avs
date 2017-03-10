/**
 * @file pcp/pcp.h  PCP protocol -- Internal interface
 *
 * Copyright (C) 2010 - 2016 Creytiv.com
 */


int pcp_payload_encode(struct mbuf *mb, enum pcp_opcode opcode,
		       const union pcp_payload *pld);

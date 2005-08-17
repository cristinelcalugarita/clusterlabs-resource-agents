/******************************************************************************
*******************************************************************************
**
**  Copyright (C) 2005 Red Hat, Inc.  All rights reserved.
**
**  This copyrighted material is made available to anyone wishing to use,
**  modify, copy, or redistribute it subject to the terms and conditions
**  of the GNU General Public License v.2.
**
*******************************************************************************
******************************************************************************/

#include "dlm_daemon.h"
#include "libcman.h"

static cman_handle_t	ch;
static cman_node_t	cluster_nodes[MAX_NODES];
static cman_node_t	new_nodes[MAX_NODES];
static int		cluster_count;
static int		member_cb;
static int		member_reason;
static int		local_nodeid;


static cman_node_t *find_cluster_node(int nodeid)
{
	int i;

	for (i = 0; i < cluster_count; i++) {
		if (cluster_nodes[i].cn_nodeid == nodeid)
			return &cluster_nodes[i];
	}
	return NULL;
}

char *nodeid2name(int nodeid)
{
	cman_node_t *cn;

	cn = find_cluster_node(nodeid);
	if (!cn)
		return NULL;
	return cn->cn_name;
}

static void process_member_cb(void)
{
	int i, rv, count;
	cman_node_t *cn;

	count = 0;
	memset(&new_nodes, 0, sizeof(new_nodes));

	rv = cman_get_nodes(ch, MAX_NODES, &count, new_nodes);
	if (rv < 0) {
		log_error("cman_get_nodes error %d %d", rv, errno);
		return;
	}

	if (count < cluster_count) {
		log_error("decrease in cluster nodes %d %d",
			  count, cluster_count);
		return;
	}

	for (i = 0; i < count; i++) {
		cn = find_cluster_node(new_nodes[i].cn_nodeid);
		if (cn)
			continue;

		/* FIXME: remove this
		   libcman appears to be returning junk after 16 bytes */
		{
			int j;
			for (j = sizeof(struct sockaddr_in);
			     j < sizeof(struct sockaddr_storage); j++)
				new_nodes[i].cn_address.cna_address[j] = 0;
		}

		set_node(new_nodes[i].cn_nodeid,
			 new_nodes[i].cn_address.cna_address,
			 (new_nodes[i].cn_nodeid == local_nodeid));
	}

	cluster_count = count;
	memcpy(cluster_nodes, new_nodes, sizeof(new_nodes));
}

static void member_callback(cman_handle_t h, void *private, int reason, int arg)
{
	member_cb = 1;
	member_reason = reason;
}

int process_member(void)
{
	while (1) {
		cman_dispatch(ch, CMAN_DISPATCH_ONE);

		if (member_cb) {
			member_cb = 0;
			process_member_cb();
		} else
			break;
	}
	return 0;
}

int setup_member(void)
{
	cman_node_t node;
	int rv, fd;

	ch = cman_init(NULL);
	if (!ch) {
		log_error("cman_init error %d %d", (int) ch, errno);
		return -ENOTCONN;
	}

	rv = cman_start_notification(ch, member_callback);
	if (rv < 0) {
		log_error("cman_start_notification error %d %d", rv, errno);
		cman_finish(ch);
		return rv;
	}

	fd = cman_get_fd(ch);

	/* FIXME: wait here for us to be a member of the cluster */
	memset(&node, 0, sizeof(node));
	rv = cman_get_node(ch, CMAN_NODEID_US, &node);
	if (rv < 0) {
		log_error("cman_get_node us error %d %d", rv, errno);
		cman_stop_notification(ch);
		cman_finish(ch);
		fd = rv;
		goto out;
	}
	local_nodeid = node.cn_nodeid;

	/* this will just initialize gd_nodes, etc */
	member_reason = CMAN_REASON_STATECHANGE;
	process_member_cb();

 out:
	return fd;
}


// SPDX-License-Identifier: GPL-2.0-only
/*
 * This is a module which is used for setting the MSS option in TCP packets.
 *
 * Copyright (C) 2000 Marc Boucher <marc@mbsi.ca>
 * Copyright (C) 2007 Patrick McHardy <kaber@trash.net>
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/module.h>
#include <linux/skbuff.h>
#include <linux/ip.h>
#include <linux/gfp.h>
#include <linux/ipv6.h>
#include <linux/tcp.h>
#include <net/dst.h>
#include <net/flow.h>
#include <net/ipv6.h>
#include <net/route.h>
#include <net/tcp.h>

#include <linux/netfilter_ipv4/ip_tables.h>
#include <linux/netfilter_ipv6/ip6_tables.h>
#include <linux/netfilter/x_tables.h>
#include <linux/netfilter/xt_tcpudp.h>
#include <linux/netfilter/xt_TCPMSS.h>

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marc Boucher <marc@mbsi.ca>");
MODULE_DESCRIPTION("Xtables: TCP Maximum Segment Size (MSS) adjustment");
MODULE_ALIAS("ipt_TCPMSS");
MODULE_ALIAS("ip6t_TCPMSS");

static inline unsigned int
optlen(const u_int8_t *opt, unsigned int offset)
{
	/* Beware zero-length options: make finite progress */
	if (opt[offset] <= TCPOPT_NOP || opt[offset+1] == 0)
		return 1;
	else
		return opt[offset+1];
}

static u_int32_t tcpmss_reverse_mtu(struct net *net,
				    const struct sk_buff *skb,
				    unsigned int family)
{
	struct flowi fl;
	struct rtable *rt = NULL;
	u_int32_t mtu     = ~0U;

	if (family == PF_INET) {
		struct flowi4 *fl4 = &fl.u.ip4;
		memset(fl4, 0, sizeof(*fl4));
		fl4->daddr = ip_hdr(skb)->saddr;
	} else {
		struct flowi6 *fl6 = &fl.u.ip6;

		memset(fl6, 0, sizeof(*fl6));
		fl6->daddr = ipv6_hdr(skb)->saddr;
	}

	nf_route(net, (struct dst_entry **)&rt, &fl, false, family);
	if (rt != NULL) {
		mtu = dst_mtu(&rt->dst);
		dst_release(&rt->dst);
	}
	return mtu;
}

static int
tcpmss_mangle_packet(struct sk_buff *skb,
		     const struct xt_action_param *par,
		     unsigned int family,
		     unsigned int tcphoff,
		     unsigned int minlen)
{
	const struct xt_tcpmss_info *info = par->targinfo;
	struct tcphdr *tcph;
	int len, tcp_hdrlen;
	unsigned int i;
	__be16 oldval;
	u16 newmss;
	u8 *opt;

	/* This is a fragment, no TCP header is available */
	if (par->fragoff != 0)
		return 0;

	if (skb_ensure_writable(skb, skb->len))
		return -1;

	len = skb->len - tcphoff;
	if (len < (int)sizeof(struct tcphdr))
		return -1;

	tcph = (struct tcphdr *)(skb_network_header(skb) + tcphoff);
	tcp_hdrlen = tcph->doff * 4;

	if (len < tcp_hdrlen || tcp_hdrlen < sizeof(struct tcphdr))
		r
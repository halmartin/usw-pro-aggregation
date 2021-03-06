/* garp
 *
 * Command to send Gratuitous ARP packets.
 *
 * Kestutis Barkauskas, Ubiquiti Networks Inc.
 *
 *
 * This program is free software; you can redistribute it
 * and/or  modify it under  the terms of  the GNU General
 * Public  License as  published  by  the  Free  Software
 * Foundation;  either  version 2 of the License, or  (at
 * your option) any later version.
 */

//usage:#define garp_trivial_usage \
//usage:   "[options] [interface]"
//usage:#define garp_full_usage "\n\n" \
//usage:   "Send Gratuitous ARP requests for interface (or all interfaces if ommited)\n" \
//usage:   "\nOptions:" \
//usage:   "\n -s n    Sleep n seconds before sending Gratuitous ARP" \
//usage:   "\n -r|-p   Send either (r)equest or re(p)ly only (default: send both)"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>		/* strcmp and friends */
#include <ctype.h>		/* isdigit and friends */
#include <stddef.h>		/* offsetof */
#include <netdb.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <net/if_arp.h>
#include <netinet/in.h>
#if __GLIBC__ >=2 && __GLIBC_MINOR__ >= 1
#include <netpacket/packet.h>
#include <net/ethernet.h>
#else
#include <linux/if_packet.h>
#include <sys/types.h>
#include <netinet/if_ether.h>
#endif
#include <time.h>
#include "busybox.h"

/* ARP packet structure definition */
typedef struct _m_arphdr {
	unsigned short int ar_hrd;          /* Format of hardware address.  */
	unsigned short int ar_pro;          /* Format of protocol address.  */
	unsigned char ar_hln;               /* Length of hardware address.  */
	unsigned char ar_pln;               /* Length of protocol address.  */
	unsigned short int ar_op;           /* ARP opcode (command).  */

	/* Ethernet looks like this : This bit is variable sized however...  */
	unsigned char __ar_sha[ETH_ALEN];   /* Sender hardware address.  */
	unsigned char __ar_sip[4];          /* Sender IP address.  */
	unsigned char __ar_tha[ETH_ALEN];   /* Target hardware address.  */
	unsigned char __ar_tip[4];          /* Target IP address.  */
} m_arphdr;


static int
send_packet(int sock, const char* ifname, int ifindex, unsigned char* buf, unsigned int buflen)
{
	static char brd_mac[] = {0xFF,0xFF,0xFF,0xFF,0xFF,0xFF};
	struct sockaddr_ll to;
	int len, i;

	memset(&to, 0, sizeof(to));
	to.sll_family = AF_PACKET;
	to.sll_ifindex = ifindex;
	memcpy(to.sll_addr, brd_mac, sizeof(brd_mac));
	to.sll_halen = sizeof(brd_mac);

	for (i = 0; i < 3; ++i)
	{
		len = sendto(sock, buf, buflen, 0, (struct sockaddr*)&to, sizeof(to));
		if (len < 0)
		{
			bb_error_msg("Interface %s can not send garp", ifname);
			return -9;
		}
	}

	return 0;
}

static int send_garp(int sock, const char* device, int reply)
{
	struct ifreq ifr;
	/*
	 * Gratuitious ARP buf
	 */
	unsigned char arp_req[42] = {
		0xff, 0xff, 0xff, 0xff, 0xff, 0xff,	/* ff:ff:ff:ff:ff:ff dst mac */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* src mac */
		0x08, 0x06,				/* ETH_P_ARP */

		0x00, 0x01,				/* ARPHRD_ETHER    */
		0x08, 0x00,				/* ETH_P_IP */
		0x06,					/* ETH_ALEN */
		0x04,					/* 4 */
		0x00, 0x01,				/* ARPOP_REQUEST */

		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* ar_sha */
		0x00, 0x00, 0x00, 0x00,			/* ar_sip */
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,	/* ff:ff:ff:ff:ff:ff ar_tha */
		0xff, 0xff, 0xff, 0xff			/* 255.255.255.255 ar_tip */
	};
	struct ether_header *eth = (struct ether_header*)arp_req;
	m_arphdr *arph		= (m_arphdr *) (arp_req + ETHER_HDR_LEN);

	strncpy(ifr.ifr_name, device, sizeof(ifr.ifr_name)-1);
	ifr.ifr_name[sizeof(ifr.ifr_name)-1] = '\0';

	if (ioctl(sock, SIOCGIFFLAGS, &ifr)) {
		bb_error_msg("SIOCGIFFLAGS");
		return -2;
	}
	if (!(ifr.ifr_flags & IFF_UP)) {
		return -3;
	}
	if (ifr.ifr_flags & (IFF_NOARP | IFF_LOOPBACK)) {
		return -4;
	}
	if (ioctl(sock, SIOCGIFADDR, &ifr) < 0  || !((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr.s_addr) {
		return -5;
	}
	memcpy(arph->__ar_sip, &((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr, 4);
	memcpy(arph->__ar_tip, &((struct sockaddr_in*)&(ifr.ifr_addr))->sin_addr, 4);
	if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
		bb_error_msg("Interface %s MAC address not found", device);
		return -6;
	}
	memcpy(eth->ether_shost, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
	memcpy(arph->__ar_sha, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

	if (reply)
	{
		memcpy(arph->__ar_tha, ifr.ifr_hwaddr.sa_data, ETH_ALEN);
		arph->ar_op = htons(ARPOP_REPLY);
	}

	if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
		bb_error_msg("Interface %s not found", device);
		return -1;
	}

	return send_packet(sock, device, ifr.ifr_ifindex, arp_req, sizeof(arp_req));
}

/*
 * Our main function.
 */

#define GARP_GETOPT_STR "s:rp"
int garp_main(int argc, char **argv);
int garp_main(int argc, char **argv)
{
	int sockfd;			/* socket fd we use to manipulate stuff with */
	struct ifconf ifc;
	struct ifreq *ifr;
	int numreqs = 30;
	int n, err = -1;
	char* out_ifc = NULL;
	int do_sleep = 0;
	int garp_request = 1;
	int garp_reply = 1;

	while ( 1 ) {
		int opt = getopt(argc, argv, GARP_GETOPT_STR);
		if (opt < 0)
			/* End of option list */
			break;
		switch (opt) {
		case 's':
			do_sleep = atoi(optarg);
			break;
		case 'r':
			garp_reply = 0;
			break;
		case 'p':
			garp_request = 0;
			break;
		default:
			bb_show_usage();
		}
	}

	if (argc == (optind + 1))
		out_ifc = argv[optind];
	else if (argc != optind) {
		bb_show_usage();
		return -1;
	}

	/* Create a channel to the NET kernel. */
	if ((sockfd = socket(PF_PACKET, SOCK_RAW, 0x300)) < 0) {
		bb_perror_msg_and_die("socket");
	}

	if (do_sleep)
		sleep(do_sleep);

	if (out_ifc)
	{
		if (garp_request) err = send_garp(sockfd, out_ifc, 0);
		if (garp_reply) err = send_garp(sockfd, out_ifc, 1);
	}
	else
	{
		ifc.ifc_buf = NULL;
		for (;;) {
			ifc.ifc_len = sizeof(struct ifreq) * numreqs;
			ifc.ifc_buf = xrealloc(ifc.ifc_buf, ifc.ifc_len);

			if (ioctl(sockfd, SIOCGIFCONF, &ifc) < 0) {
				perror("SIOCGIFCONF");
				goto out;
			}
			if (ifc.ifc_len == sizeof(struct ifreq) * numreqs) {
				/* assume it overflowed and try again */
				numreqs += 10;
				continue;
			}
			break;
		}

		ifr = ifc.ifc_req;
		for (n = 0; n < ifc.ifc_len; n += sizeof(struct ifreq)) {
			if (garp_request) err = send_garp(sockfd, ifr->ifr_name, 0);
			if (garp_reply) err = send_garp(sockfd, ifr->ifr_name, 1);
			ifr++;
		}
out:
		free(ifc.ifc_buf);
	}

	close(sockfd);
	return err;
}

/*
 * Copyright (c) 1997-2015 Red Hat, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
 *
 * Authors:
 *   Erik Troan <ewt@redhat.com>
 *   Preston Brown <pbrown@redhat.com>
 *   David Cantrell <dcantrell@redhat.com>
 *   Nikos Mavrogiannopoulos <nmav@redhat.com>
 */

#define _GNU_SOURCE		/* asprintf */
#include <ctype.h>
#include <popt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <errno.h>

/*!
  \file ipcalc.c
  \brief provides utilities for manipulating IP addresses.

  ipcalc provides utilities and a front-end command line interface for
  manipulating IP addresses, and calculating various aspects of an ip
  address/netmask/network address/prefix/etc.

  Functionality can be accessed from other languages from the library
  interface, documented here.  To use ipcalc from the shell, read the
  ipcalc(1) manual page.

  When passing parameters to the various functions, take note of whether they
  take host byte order or network byte order.  Most take host byte order, and
  return host byte order, but there are some exceptions.
*/

int safe_atoi(const char *s, int *ret_i)
{
	char *x = NULL;
	long l;

	errno = 0;
	l = strtol(s, &x, 0);

	if (!x || x == s || *x || errno)
		return errno > 0 ? -errno : -EINVAL;

	if ((long)(int)l != l)
		return -ERANGE;

	*ret_i = (int)l;
	return 0;
}

/*!
  \fn struct in_addr prefix2mask(int bits)
  \brief creates a netmask from a specified number of bits

  This function converts a prefix length to a netmask.  As CIDR (classless
  internet domain internet domain routing) has taken off, more an more IP
  addresses are being specified in the format address/prefix
  (i.e. 192.168.2.3/24, with a corresponding netmask 255.255.255.0).  If you
  need to see what netmask corresponds to the prefix part of the address, this
  is the function.  See also \ref mask2prefix.

  \param prefix is the number of bits to create a mask for.
  \return a network mask, in network byte order.
*/
struct in_addr prefix2mask(int prefix)
{
	struct in_addr mask;
	memset(&mask, 0, sizeof(mask));
	if (prefix) {
		mask.s_addr = htonl(~((1 << (32 - prefix)) - 1));
	} else {
		mask.s_addr = htonl(0);
	}
	return mask;
}

/*!
  \fn struct in_addr default_netmask(struct in_addr addr)

  \brief returns the default (canonical) netmask associated with specified IP
  address.

  When the Internet was originally set up, various ranges of IP addresses were
  segmented into three network classes: A, B, and C.  This function will return
  a netmask that is associated with the IP address specified defining where it
  falls in the predefined classes.

  \param addr an IP address in network byte order.
  \return a netmask in network byte order.  */
struct in_addr default_netmask(struct in_addr addr)
{
	uint32_t saddr = addr.s_addr;
	struct in_addr mask;

	memset(&mask, 0, sizeof(mask));

	if (((ntohl(saddr) & 0xFF000000) >> 24) <= 127)
		mask.s_addr = htonl(0xFF000000);
	else if (((ntohl(saddr) & 0xFF000000) >> 24) <= 191)
		mask.s_addr = htonl(0xFFFF0000);
	else
		mask.s_addr = htonl(0xFFFFFF00);

	return mask;
}

/*!
  \fn struct in_addr calc_broadcast(struct in_addr addr, int prefix)

  \brief calculate broadcast address given an IP address and a prefix length.

  \param addr an IP address in network byte order.
  \param prefix a prefix length.

  \return the calculated broadcast address for the network, in network byte
  order.
*/
struct in_addr calc_broadcast(struct in_addr addr, int prefix)
{
	struct in_addr mask = prefix2mask(prefix);
	struct in_addr broadcast;

	memset(&broadcast, 0, sizeof(broadcast));
	broadcast.s_addr = (addr.s_addr & mask.s_addr) | ~mask.s_addr;
	return broadcast;
}

/*!
  \fn struct in_addr calc_network(struct in_addr addr, int prefix)
  \brief calculates the network address for a specified address and prefix.

  \param addr an IP address, in network byte order
  \param prefix the network prefix
  \return the base address of the network that addr is associated with, in
  network byte order.
*/
struct in_addr calc_network(struct in_addr addr, int prefix)
{
	struct in_addr mask = prefix2mask(prefix);
	struct in_addr network;

	memset(&network, 0, sizeof(network));
	network.s_addr = addr.s_addr & mask.s_addr;
	return network;
}

/*!
  \fn const char *get_hostname(int family, void *addr)
  \brief returns the hostname associated with the specified IP address

  \param family the address family, either AF_INET or AF_INET6.
  \param addr an IP address to find a hostname for, in network byte order,
  should either be a pointer to a struct in_addr or a struct in6_addr.

  \return a hostname, or NULL if one cannot be determined.  Hostname is stored
  in a static buffer that may disappear at any time, the caller should copy the
  data if it needs permanent storage.
*/
char *get_hostname(int family, void *addr)
{
	struct hostent *hostinfo = NULL;
	int x;
	struct in_addr addr4;
	struct in6_addr addr6;

	if (family == AF_INET) {
		memset(&addr4, 0, sizeof(addr4));
		memcpy(&addr4, addr, sizeof(addr4));
		hostinfo = gethostbyaddr((const void *)&addr4,
					 sizeof(addr4), family);
	} else if (family == AF_INET6) {
		memset(&addr6, 0, sizeof(addr6));
		memcpy(&addr6, addr, sizeof(addr6));
		hostinfo = gethostbyaddr((const void *)&addr6,
					 sizeof(addr6), family);
	}

	if (!hostinfo)
		return NULL;

	for (x = 0; hostinfo->h_name[x]; x++) {
		hostinfo->h_name[x] = tolower(hostinfo->h_name[x]);
	}
	return hostinfo->h_name;
}

int bit_count(uint32_t i)
{
	int c = 0;
	unsigned int seen_one = 0;

	while (i > 0) {
		if (i & 1) {
			seen_one = 1;
			c++;
		} else {
			if (seen_one) {
				return -1;
			}
		}
		i >>= 1;
	}

	if (c == 0)
		return -1;
	return c;
}

/*!
  \fn int mask2prefix(struct in_addr mask)
  \brief calculates the number of bits masked off by a netmask.

  This function calculates the significant bits in an IP address as specified by
  a netmask.  See also \ref prefix2mask.

  \param mask is the netmask, specified as an struct in_addr in network byte order.
  \return the number of significant bits.  */
int ipv4_mask_to_int(const char *prefix)
{
	int ret;
	struct in_addr in;

	ret = inet_pton(AF_INET, prefix, &in);
	if (ret == 0)
		return -1;

	return bit_count(ntohl(in.s_addr));
}

typedef struct ip_info_st {
	char *expanded_ip;
	char *expanded_network;

	char *network;
	char *broadcast;	/* ipv4 only */
	char *netmask;
	char *hostname;
	unsigned prefix;

	char *hostmin;
	char *hostmax;
	const char *type;
} ip_info_st;

char *ipv4_net_to_type(struct in_addr net)
{
	unsigned byte1 = (ntohl(net.s_addr) >> 24) & 0xff;
	unsigned byte2 = (ntohl(net.s_addr) >> 16) & 0xff;
	unsigned byte3 = (ntohl(net.s_addr) >> 8) & 0xff;
	unsigned byte4 = (ntohl(net.s_addr)) & 0xff;

	/* based on IANA's iana-ipv4-special-registry and ipv4-address-space
	 * Updated: 2015-05-12
	 */
	if (byte1 == 0) {
		return "This host on this network";
	}

	if (byte1 == 10) {
		return "Private Use";
	}

	if (byte1 == 100 && (byte2 & 0xc0) == 64) {
		return "Shared Address Space";
	}

	if (byte1 == 127) {
		return "Loopback";
	}

	if (byte1 == 169 && byte2 == 254) {
		return "Link Local";
	}

	if (byte1 == 172 && (byte2 & 0xf0) == 16) {
		return "Private Use";
	}

	if (byte1 == 192 && byte2 == 0 && byte3 == 0) {
		return "IETF Protocol Assignments";
	}

	if (byte1 == 192 && byte2 == 2 && byte3 == 0) {
		return "Documentation (TEST-NET-1)";
	}

	if (byte1 == 192 && byte2 == 51 && byte3 == 100) {
		return "Documentation (TEST-NET-2)";
	}

	if (byte1 == 203 && byte2 == 0 && byte3 == 113) {
		return "Documentation (TEST-NET-3)";
	}

	if (byte1 == 192 && byte2 == 88 && byte3 == 99) {
		return "6 to 4 Relay Anycast (Deprecated)";
	}

	if (byte1 == 192 && byte2 == 52 && byte3 == 193) {
		return "AMT";
	}

	if (byte1 == 192 && byte2 == 168) {
		return "Private Use";
	}

	if (byte1 == 255 && byte2 == 255 && byte3 == 255 && byte4 == 255) {
		return "Limited Broadcast";
	}

	if (byte1 == 192 && (byte2 & 0xfe) == 18) {
		return "Private Use";
	}

	if (byte1 >= 224 && byte1 <= 239) {
		return "Multicast";
	}

	if ((byte1 & 0xf0) == 240) {
		return "Reserved";
	}

	return "Internet or Reserved for Future use";
}

int get_ipv4_info(const char *ipStr, int prefix, ip_info_st * info,
		  int beSilent, int showHostname)
{
	struct in_addr ip, netmask, network, broadcast, minhost, maxhost;
	char namebuf[INET6_ADDRSTRLEN + 1];
	char errBuf[250];

	memset(info, 0, sizeof(*info));

	/* Handle CIDR entries such as 172/8 */
	if (prefix >= 0) {
		char *tmp = (char *)ipStr;
		int i;

		for (i = 3; i > 0; i--) {
			tmp = strchr(tmp, '.');
			if (!tmp)
				break;
			else
				tmp++;
		}

		tmp = NULL;
		for (; i > 0; i--) {
			if (asprintf(&tmp, "%s.0", ipStr) == -1) {
				fprintf(stderr,
					"Memory allocation failure line %d\n",
					__LINE__);
				abort();
			}
			ipStr = tmp;
		}
	} else {		/* assume single host */
		prefix = 32;
	}

	if (inet_pton(AF_INET, ipStr, &ip) <= 0) {
		if (!beSilent)
			fprintf(stderr, "ipcalc: bad IPv4 address: %s\n",
				ipStr);
		return -1;
	} else if (prefix > 32) {
		if (!beSilent)
			fprintf(stderr, "ipcalc: bad IPv4 prefix %d\n", prefix);
		return -1;
	}

	netmask = prefix2mask(prefix);
	memset(&namebuf, '\0', sizeof(namebuf));

	if (inet_ntop(AF_INET, &netmask, namebuf, INET_ADDRSTRLEN) == NULL) {
		fprintf(stderr, "Memory allocation failure line %d\n",
			__LINE__);
		abort();
	}
	info->netmask = strdup(namebuf);
	info->prefix = prefix;

	broadcast = calc_broadcast(ip, prefix);

	memset(&namebuf, '\0', sizeof(namebuf));
	if (inet_ntop(AF_INET, &broadcast, namebuf, INET_ADDRSTRLEN) == NULL) {
		fprintf(stderr, "Memory allocation failure line %d\n",
			__LINE__);
		abort();
	}
	info->broadcast = strdup(namebuf);

	network = calc_network(ip, prefix);

	memset(&namebuf, '\0', sizeof(namebuf));
	if (inet_ntop(AF_INET, &network, namebuf, INET_ADDRSTRLEN) == NULL) {
		fprintf(stderr, "Memory allocation failure line %d\n",
			__LINE__);
		abort();
	}

	info->network = strdup(namebuf);

	info->type = ipv4_net_to_type(network);

	if (prefix < 32) {
		memcpy(&minhost, &network, sizeof(minhost));

		if (prefix <= 30)
			minhost.s_addr = htonl(ntohl(minhost.s_addr) | 1);
		if (inet_ntop(AF_INET, &minhost, namebuf, INET_ADDRSTRLEN) ==
		    NULL) {
			fprintf(stderr, "Memory allocation failure line %d\n",
				__LINE__);
			abort();
		}
		info->hostmin = strdup(namebuf);

		memcpy(&maxhost, &network, sizeof(minhost));
		maxhost.s_addr |= ~netmask.s_addr;
		if (prefix <= 30) {
			maxhost.s_addr = htonl(ntohl(maxhost.s_addr) - 1);
		}
		if (inet_ntop(AF_INET, &maxhost, namebuf, sizeof(namebuf)) == 0) {
			if (!beSilent)
				fprintf(stderr,
					"ipcalc: error calculating the IPv6 network\n");
			return -1;
		}

		info->hostmax = strdup(namebuf);
	} else {
		info->hostmin = info->network;
		info->hostmax = info->network;
	}

	if (showHostname) {
		info->hostname = get_hostname(AF_INET, &ip);

		if (info->hostname == NULL) {
			if (!beSilent) {
				sprintf(errBuf,
					"ipcalc: cannot find hostname for %s",
					ipStr);
				herror(errBuf);
			}
			return -1;
		}
	}

	return 0;
}

char *ipv6_prefix_to_mask(unsigned prefix, struct in6_addr *mask)
{
	struct in6_addr in6;
	int i, j;
	char buf[128];

	if (prefix == 0 || prefix > 128)
		return NULL;

	memset(&in6, 0x0, sizeof(in6));
	for (i = prefix, j = 0; i > 0; i -= 8, j++) {
		if (i >= 8) {
			in6.s6_addr[j] = 0xff;
		} else {
			in6.s6_addr[j] = (unsigned long)(0xffU << (8 - i));
		}
	}

	if (inet_ntop(AF_INET6, &in6, buf, sizeof(buf)) == NULL)
		return NULL;

	memcpy(mask, &in6, sizeof(*mask));
	return strdup(buf);
}

char *ipv6_net_to_type(struct in6_addr *net)
{
	uint16_t word1 = net->s6_addr[0] << 8 | net->s6_addr[1];
	uint16_t word2 = net->s6_addr[2] << 8 | net->s6_addr[3];

	/* based on IANA's iana-ipv6-special-registry and ipv6-address-space 
	 * Updated: 2015-05-12
	 */
	if (memcmp
	    (net->s6_addr,
	     "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x01",
	     16) == 0)
		return "Loopback Address";

	if (memcmp
	    (net->s6_addr,
	     "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
	     16) == 0)
		return "Unspecified Address";

	if (memcmp
	    (net->s6_addr, "\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\xff\xff",
	     12) == 0)
		return "IPv4-mapped Address";

	if (memcmp
	    (net->s6_addr, "\x00\x64\xff\x9b\x00\x00\x00\x00\x00\x00\x00\x00",
	     12) == 0)
		return "IPv4-IPv6 Translat.";

	if (memcmp
	    (net->s6_addr, "\x10\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00\x00",
	     12) == 0)
		return "Discard-Only Address Block";

	if ((word1 & 0xfffe) == 0x2001 && word2 == 0)
		return "IETF Protocol Assignments";

	if ((word1 & 0xe000) == 0x2000) {
		return "Global Unicast";
	}

	if (((net->s6_addr[0] & 0xfe) == 0xfc)) {
		return "Unique Local Unicast";
	}

	if ((word1 & 0xffc0) == 0xfe80) {
		return "Link-Scoped Unicast";
	}

	if ((net->s6_addr[0] & 0xff) == 0xff) {
		return "Multicast";
	}

	if ((word1 & 0xfffe) == 0x2002)
		return "6to4";

	return "Reserved";
}

static
char *expand_ipv6(struct in6_addr *ip6)
{
	char buf[128];
	char *p;
	unsigned i;

	p = buf;
	for (i = 0; i < 16; i++) {
		sprintf(p, "%.2x", (unsigned)ip6->s6_addr[i]);
		p += 2;
		if (i % 2 != 0 && i != 15) {
			*p = ':';
			p++;
		}
	}
	*p = 0;

	return strdup(buf);
}

int get_ipv6_info(const char *ipStr, int prefix, ip_info_st * info,
		  int beSilent, int showHostname)
{
	struct in6_addr ip6, mask, network;
	char errBuf[250];
	unsigned i;

	memset(info, 0, sizeof(*info));

	if (inet_pton(AF_INET6, ipStr, &ip6) <= 0) {
		if (!beSilent)
			fprintf(stderr, "ipcalc: bad IPv6 address: %s\n",
				ipStr);
		return -1;
	}

	/* expand  */
	info->expanded_ip = expand_ipv6(&ip6);

	if (prefix == 0 || prefix > 128) {
		if (!beSilent)
			fprintf(stderr, "ipcalc: bad IPv6 prefix: %d\n",
				prefix);
		return -1;
	} else if (prefix < 0) {
		prefix = 128;
	}

	info->prefix = prefix;

	info->netmask = ipv6_prefix_to_mask(prefix, &mask);
	if (!info->netmask) {
		if (!beSilent)
			fprintf(stderr,
				"ipcalc: error converting IPv6 prefix: %d\n",
				prefix);
		return -1;
	}

	for (i = 0; i < sizeof(struct in6_addr); i++)
		network.s6_addr[i] = ip6.s6_addr[i] & mask.s6_addr[i];

	if (inet_ntop(AF_INET6, &network, errBuf, sizeof(errBuf)) == 0) {
		if (!beSilent)
			fprintf(stderr,
				"ipcalc: error calculating the IPv6 network\n");
		return -1;
	}

	info->network = strdup(errBuf);

	info->expanded_network = expand_ipv6(&network);
	info->type = ipv6_net_to_type(&network);

	if (prefix < 128) {
		info->hostmin = strdup(errBuf);

		for (i = 0; i < sizeof(struct in6_addr); i++)
			network.s6_addr[i] |= ~mask.s6_addr[i];
		if (inet_ntop(AF_INET6, &network, errBuf, sizeof(errBuf)) == 0) {
			if (!beSilent)
				fprintf(stderr,
					"ipcalc: error calculating the IPv6 network\n");
			return -1;
		}

		info->hostmax = strdup(errBuf);
	} else {
		info->hostmin = info->network;
		info->hostmax = info->network;
	}

	if (showHostname) {
		info->hostname = get_hostname(AF_INET6, &ip6);
		if (info->hostname == NULL) {
			if (!beSilent) {
				sprintf(errBuf,
					"ipcalc: cannot find hostname for %s",
					ipStr);
				herror(errBuf);
			}
			return -1;
		}
	}
	return 0;
}

/*!
  \fn main(int argc, const char **argv)
  \brief wrapper program for ipcalc functions.

  This is a wrapper program for the functions that the ipcalc library provides.
  It can be used from shell scripts or directly from the command line.

  For more information, please see the ipcalc(1) man page.
*/
int main(int argc, const char **argv)
{
	int showBroadcast = 0, showPrefix = 0, showNetwork = 0;
	int showHostname = 0, showNetmask = 0, showAddrSpace = 0;
	int showHostMax = 0, showHostMin = 0;
	int beSilent = 0;
	int doCheck = 0, familyIPv6 = 0, doInfo = 0;
	int rc;
	poptContext optCon;
	char *ipStr, *prefixStr, *netmaskStr = NULL, *chptr;
	int prefix = -1;
	ip_info_st info;
	int r = 0;

	struct poptOption optionsTable[] = {
		{"check", 'c', 0, &doCheck, 0,
		 "Validate IP address",},
		{"info", 'i', 0, &doInfo, 0,
		 "Print information on the provided IP address",},
		{"ipv4", '4', 0, NULL, 0,
		 "IPv4 address family (deprecated)",},
		{"ipv6", '6', 0, NULL, 0,
		 "IPv6 address family (deprecated)",},
		{"broadcast", 'b', 0, &showBroadcast, 0,
		 "Display calculated broadcast address",},
		{"hostname", 'h', 0, &showHostname, 0,
		 "Show hostname determined via DNS"},
		{"netmask", 'm', 0, &showNetmask, 0,
		 "Display default netmask for IP (class A, B, or C)"},
		{"network", 'n', 0, &showNetwork, 0,
		 "Display network address",},
		{"prefix", 'p', 0, &showPrefix, 0,
		 "Display network prefix",},
		{"minaddr", '\0', 0, &showHostMin, 0,
		 "Display the minimum address in the network",},
		{"maxaddr", '\0', 0, &showHostMax, 0,
		 "Display the maximum address in the network",},
		{"addrspace", '\0', 0, &showAddrSpace, 0,
		 "Display the address space the network resides on",},
		{"silent", 's', 0, &beSilent, 0,
		 "Don't ever display error messages"},
		POPT_AUTOHELP {NULL, '\0', 0, 0, 0, NULL, NULL}
	};

	optCon = poptGetContext("ipcalc", argc, argv, optionsTable, 0);
	poptReadDefaultConfig(optCon, 1);

	if ((rc = poptGetNextOpt(optCon)) < -1) {
		if (!beSilent) {
			fprintf(stderr, "ipcalc: bad argument %s: %s\n",
				poptBadOption(optCon, POPT_BADOPTION_NOALIAS),
				poptStrerror(rc));
			poptPrintHelp(optCon, stderr, 0);
		}
		return 1;
	}

	if (!(ipStr = (char *)poptGetArg(optCon))) {
		if (!beSilent) {
			fprintf(stderr, "ipcalc: ip address expected\n");
			poptPrintHelp(optCon, stderr, 0);
		}
		return 1;
	}

	/* if there is a : in the address, it is an IPv6 address */
	if (strchr(ipStr, ':') != NULL) {
		familyIPv6 = 1;
	}

	if (strchr(ipStr, '/') != NULL) {
		prefixStr = strchr(ipStr, '/') + 1;
		prefixStr--;
		*prefixStr = '\0';	/* fix up ipStr */
		prefixStr++;
	} else {
		prefixStr = NULL;
	}

	if (prefixStr != NULL) {
		if (!familyIPv6 && strchr(prefixStr, '.')) {	/* prefix is 255.x.x.x */
			prefix = ipv4_mask_to_int(prefixStr);
		} else {
			r = safe_atoi(prefixStr, &prefix);
			if (r != 0) {
				if (!beSilent)
					fprintf(stderr,
						"ipcalc: bad prefix: %s\n",
						prefixStr);
				return 1;
			}
		}

		if (prefix < 0 || ((familyIPv6 && prefix > 128)
				   || (!familyIPv6 && prefix > 32))) {
			if (!beSilent)
				fprintf(stderr, "ipcalc: bad prefix: %s\n",
					prefixStr);
			return 1;
		}
	}

	if (familyIPv6) {
		r = get_ipv6_info(ipStr, prefix, &info, beSilent, showHostname);
	} else {
		if (showBroadcast || showNetwork || showPrefix) {
			if (!(netmaskStr = (char *)poptGetArg(optCon))
			    && (prefix < 0)) {
				if (!beSilent) {
					fprintf(stderr,
						"ipcalc: netmask or prefix expected\n");
					poptPrintHelp(optCon, stderr, 0);
				}
				return 1;
			} else if (netmaskStr && prefix >= 0) {
				if (!beSilent) {
					fprintf(stderr,
						"ipcalc: both netmask and prefix specified\n");
					poptPrintHelp(optCon, stderr, 0);
				}
				return 1;
			}
		}

		if (prefix == -1 && netmaskStr) {
			prefix = ipv4_mask_to_int(netmaskStr);
			if (prefix < 0) {
				if (!beSilent)
					fprintf(stderr,
						"ipcalc: bad prefix: %s\n",
						prefixStr);
				return 1;
			}
		}
		r = get_ipv4_info(ipStr, prefix, &info, beSilent, showHostname);
	}

	if (r < 0) {
		if (!beSilent)
			fprintf(stderr,
				"ipcalc: error calculating network: %s/%u\n",
				ipStr, prefix);
		return 1;
	}

	if ((chptr = (char *)poptGetArg(optCon))) {
		if (!beSilent) {
			fprintf(stderr, "ipcalc: unexpected argument: %s\n",
				chptr);
			poptPrintHelp(optCon, stderr, 0);
		}
		return 1;
	}

	if (doCheck)
		return 0;

	/* if no option is given, print information on IP */
	if (!(showNetmask | showPrefix | showBroadcast | showNetwork |
	      showHostMin | showHostMax | showHostname | doInfo |
	      showAddrSpace)) {
		doInfo = 1;
	}

	poptFreeContext(optCon);

	/* we know what we want to display now, so display it. */
	if (doInfo) {
		if (info.expanded_ip)
			printf("Full Address:\t%s\n", info.expanded_ip);
		printf("Address:\t%s\n", ipStr);

		if ((familyIPv6 && info.prefix != 128) ||
		    (!familyIPv6 && info.prefix != 32)) {
			printf("Netmask:\t%s = %u\n", info.netmask,
			       info.prefix);
			if (info.expanded_network)
				printf("Full Network:\t%s\n",
				       info.expanded_network);
			printf("Network:\t%s/%u\n", info.network, info.prefix);
			if (info.type)
				printf("Address space:\t%s\n", info.type);

			if (info.broadcast)
				printf("Broadcast:\t%s\n", info.broadcast);
			printf("\n");

			if (info.hostmin)
				printf("HostMin:\t%s\n", info.hostmin);
			if (info.hostmax)
				printf("HostMax:\t%s\n", info.hostmax);

			if (!familyIPv6) {
				unsigned hosts;
				if (info.prefix >= 31)
					hosts = (1 << (32 - info.prefix));
				else
					hosts = (1 << (32 - info.prefix)) - 2;
				printf("Hosts/Net:\t%u\n", hosts);
			} else {
				if (info.prefix < sizeof(long) * 8 + 1)
					printf("Hosts/Net:\t2^(%u)\n",
					       (128 - info.prefix));
				else
					printf("Hosts/Net:\t%lu\n",
					       (unsigned long)1 << (128 -
								    info.
								    prefix));
			}
		} else {
			if (info.type)
				printf("Address space:\t%s\n", info.type);

		}
	} else {

		if (showNetmask) {
			printf("NETMASK=%s\n", info.netmask);
		}

		if (showPrefix) {
			printf("PREFIX=%u\n", info.prefix);
		}

		if (showBroadcast && !familyIPv6) {
			printf("BROADCAST=%s\n", info.broadcast);
		}

		if (showNetwork) {
			printf("NETWORK=%s\n", info.network);
		}

		if (showHostMin && info.hostmin) {
			printf("MINADDR=%s\n", info.hostmin);
		}

		if (showHostMax && info.hostmax) {
			printf("MAXADDR=%s\n", info.hostmax);
		}

		if (showAddrSpace && info.type) {
			printf("ADDRSPACE=\"%s\"\n", info.type);
		}

		if (showHostname) {
			printf("HOSTNAME=%s\n", info.hostname);
		}
	}

	return 0;
}

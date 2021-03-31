#ifndef _IP_H
#define _IP_H

#include <netinet/in.h>
#define NONE_IP	(1<<0)
#define USE_IPV4		(1<<1)
#define USE_IPV6		(1<<2)
#define SOCKADDR4_LEN	(sizeof(struct sockaddr_in))
#define SOCKADDR6_LEN	(sizeof(struct sockaddr_in6))

typedef struct jujube_in_addr {
	union {
		struct in_addr ipv4_addr;
		struct in6_addr ipv6_addr;	
	} inx_addr;
	unsigned int flag;
} jujube_in_addr;

int check_host_ip_and_get_verion(char *server_ip/*in*/, struct jujube_in_addr *jaddr/*in-out*/);
const char *ip_2_str(struct jujube_in_addr *in_addr);

#endif


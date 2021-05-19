#include "ip.h"
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <ctype.h>
#include <netinet/in.h>
#include <arpa/inet.h>

void jujube_in_addr_init(struct jujube_in_addr *jaddr) {
	memset(&(jaddr->inx_addr), 0, sizeof(jaddr->inx_addr));
	jaddr->flag = NONE_IP;
	return;
}

int jujube_pton(int family, const char *ip, struct jujube_in_addr *jaddr) {
	struct in6_addr	in6_val;
	memset(&in6_val, 0, sizeof(in6_val));
	int ret = 0;

	if (!(family == AF_INET || family == AF_INET6)) {
		ret = -1;
		goto end;
	}
	if ((ret = inet_pton(family, ip, &in6_val)) != 1) {
		goto end;
	}
	memcpy(&jaddr->inx_addr, &in6_val, sizeof(jaddr->inx_addr));
	end:
	return ret;
}

static int get_str_num(char *buf) {
	int i = 0;
	char *p = buf;
	int blank = 0;

	while ((*p) && (*p == ' ' || *p == '\t'))
		p++;
	while (*p) {
		if ((*p != ' ') && (*p != '\t')) {
			blank++;
			if (blank == 1)
				i++;
		} else
			blank = 0;
		p++;
	}
	return i;
}

static int check_blank(char *instr) {
	int num;
	char *p = instr;

	if ((p == NULL) || (strcmp(p, "") == 0))
		return 0;
	num = get_str_num(p);
	if (num == 0)
		return 0;
	else if (num == 1) {
		//	sscanf(p, "%s", instr);
		return 1;
	} else
		return -1;
}

static int check_symbol(char *instr, char symbol) {
	int i = 0;
	char *p = instr;

	while (*p) {
		i += (*p++ == symbol);
	}
	return i;
}

static int my_atoi(const char *p)
{
	while (*p == '0') {
		p++;
	}
	if (*p == '\0')
		return 0;
	return atoi(p);
}

static int dotted_to_ipaddr(char *dotted, uint32_t *ipaddr) {
	unsigned char *addrp;
	char *p, *q;
	int onebyte, i, symbol;
	int validip[4];
	char buf[20];
	int blank, validlen = 0;

	/* copy dotted string, because we need to modify it */
	blank = check_blank(dotted);
	if (blank == 1) {
		symbol = check_symbol(dotted, '.');
		if (symbol != 3)
			return -1;
		p = dotted;
		while (*p) {
			if ((*p == '.') && (*(++p) == '.'))
				return -1;
			p++;
		}
		strncpy(buf, dotted, sizeof(buf) - 1);
		buf[20-1] = '\0';
		addrp = (unsigned char *) (ipaddr);
		p = dotted;
		for (i = 0; i < strlen(dotted); i++) {
			if ((!isdigit(*(p + i))) && (*(p + i) != '.'))
				return -1;
		}
		p = buf;
		for (i = 0; i < 3; i++) {
			if ((q = strchr(p, '.')) == NULL)	/* find the "." */
				return -1;
			else {
				*q = '\0';
				if ((onebyte = my_atoi(p)) < 0 || onebyte > 255) {
					return -1;
				} else {
					validip[i] = onebyte;
					addrp[i] = (unsigned char) onebyte;
					validlen += strlen(p) + 1;
				}
			}
			p = q + 1;
		}
		/* we've checked 3 bytes, now we check the last one */
		if (!isdigit(*p))
			return -1;
		q = p;
		while (isdigit(*q))
			q++;
		*q = '\0';
		if ((onebyte = my_atoi(p)) < 0 || onebyte > 255)
			return -1;
		else {
			validip[i] = onebyte;
			addrp[3] = (unsigned char) onebyte;
			validlen += strlen(p);
		}
		sprintf(dotted, "%d.%d.%d.%d", validip[0], validip[1], validip[2], validip[3]);
		return 1;	/* success */
	} else
		return blank;
}

int check_host_ipv4(char *ip) {
	uint8_t *p;
	uint32_t tempip;
	int result;

	result = dotted_to_ipaddr(ip, &tempip);
	if (result == 1) {
		p = (uint8_t *)&tempip;
		if (p[0] == 0 || (p[0] > 223)||(p[3]==255) || (p[0]==127 )) {
			return -1;
		} else {
			return 1;
		}
	} else if (result == -1) {
		return -1;
	}
}

int check_host_ipv6(struct jujube_in_addr *jaddr) {
	int ret = 0;
	if (IN6_IS_ADDR_UNSPECIFIED(&jaddr->inx_addr.ipv6_addr)) {
		ret = -1;
		goto end;
	}
	if (IN6_IS_ADDR_LOOPBACK(&jaddr->inx_addr.ipv6_addr)) {
		ret = -2;
		goto end;
	}
	end:
	return ret;
}

int check_host_ip(char* server_ip, struct jujube_in_addr *jaddr) {
	int ret = 0;
	int flag = jaddr->flag;
	if (flag & USE_IPV4) {
		if ((ret = check_host_ipv4(server_ip)) != 1) {
			ret = -1;
			goto end;
		} else {
			ret = 0;
			goto end;
		}
	} else if (flag & USE_IPV6) {
		if((ret = check_host_ipv6(jaddr)) < 0){
			goto end;
		}
	}
	end:
	return ret;
}

int check_host_ip_and_get_verion(char *server_ip, struct jujube_in_addr *jaddr) {
	int ret = 0;
	struct jujube_in_addr tmp_addr;
	jujube_in_addr_init(&tmp_addr);

	if ((ret = jujube_pton(AF_INET, server_ip, &tmp_addr)) == 1) {
		tmp_addr.flag = USE_IPV4;
	} else if(ret == 0) {
		if ((ret = jujube_pton(AF_INET6, server_ip, &tmp_addr)) != 1) {
			ret = -1;
			goto end;
		}
		tmp_addr.flag = USE_IPV6;
	} else {
		ret = -2;
		goto end;
	}
	memcpy(jaddr, &tmp_addr, sizeof(tmp_addr));
	ret = check_host_ip(server_ip, jaddr);
	end:
	return ret;
}

const char *ip_2_str(struct jujube_in_addr *in_addr) {
	static char str[INET6_ADDRSTRLEN] = {'\0'};
	struct in6_addr *in_addr_ptr= (struct in6_addr *)(&in_addr->inx_addr);
	const char *p = inet_ntop(in_addr->flag == USE_IPV4 ? AF_INET : AF_INET6,
							  (const void *)in_addr_ptr, str, sizeof(str));

	if (!p) {
		str[0] = '\0';
	}
	return str;
}



/* -*- indent-tabs-mode: nil; -*- */
#include "winutils.h"
#include "portfwd.h"

#ifndef RT_OS_WINDOWS
#include <arpa/inet.h>
#include <poll.h>
#else
# include "winpoll.h"
#endif
#include <stdio.h>
#include <string.h>

#include "proxytest.h"
#include "proxy_pollmgr.h"
#include "pxremap.h"

#include "lwip/netif.h"


struct portfwd_msg {
    struct fwspec *fwspec;
    int add;
};


static int portfwd_chan_send(struct portfwd_msg *);
static int portfwd_rule_add_del(struct fwspec *, SOCKET);
static int portfwd_pmgr_chan(struct pollmgr_handler *, SOCKET, int);


static struct pollmgr_handler portfwd_pmgr_chan_hdl;


void
portfwd_init(void)
{
    portfwd_pmgr_chan_hdl.callback = portfwd_pmgr_chan;
    portfwd_pmgr_chan_hdl.data = NULL;
    portfwd_pmgr_chan_hdl.slot = -1;
    pollmgr_add_chan(POLLMGR_CHAN_PORTFWD, &portfwd_pmgr_chan_hdl);

    /* add preconfigured forwarders */
    fwtcp_init();
    fwudp_init();
}


static int
portfwd_chan_send(struct portfwd_msg *msg)
{
    ssize_t nsent;

    nsent = pollmgr_chan_send(POLLMGR_CHAN_PORTFWD, &msg, sizeof(msg));
    if (nsent < 0) {
        free(msg);
        return -1;
    }

    return 0;
}


static int
portfwd_rule_add_del(struct fwspec *fwspec, int add)
{
    struct portfwd_msg *msg;

    msg = (struct portfwd_msg *)malloc(sizeof(*msg));
    if (msg == NULL) {
        return -1;
    }

    msg->fwspec = fwspec;
    msg->add = add;

    return portfwd_chan_send(msg);
}


int
portfwd_rule_add(struct fwspec *fwspec)
{
    return portfwd_rule_add_del(fwspec, 1);
}


int
portfwd_rule_del(struct fwspec *fwspec)
{
    return portfwd_rule_add_del(fwspec, 0);
}


/**
 * POLLMGR_CHAN_PORTFWD handler.
 */
static int
portfwd_pmgr_chan(struct pollmgr_handler *handler, SOCKET fd, int revents)
{
    void *ptr = pollmgr_chan_recv_ptr(handler, fd, revents);
    struct portfwd_msg *msg = (struct portfwd_msg *)ptr;

    if (msg->fwspec->stype == SOCK_STREAM) {
        if (msg->add) {
            fwtcp_add(msg->fwspec);
        }
        else {
            fwtcp_del(msg->fwspec);
        }
    }
    else { /* SOCK_DGRAM */
        if (msg->add) {
            fwudp_add(msg->fwspec);
        }
        else {
            fwudp_del(msg->fwspec);
        }
    }

    free(msg->fwspec);
    free(msg);

    return POLLIN;
}



int
fwspec_set(struct fwspec *fwspec, int sdom, int stype,
           const char *src_addr_str, uint16_t src_port,
           const char *dst_addr_str, uint16_t dst_port)
{
    int status;
    int saf;
    void *src_addr, *dst_addr;

    LWIP_ASSERT1(sdom == PF_INET || sdom == PF_INET6);
    LWIP_ASSERT1(stype == SOCK_STREAM || stype == SOCK_DGRAM);

    fwspec->sdom = sdom;
    fwspec->stype = stype;

    if (sdom == PF_INET) {
        struct sockaddr_in *src = &fwspec->src.sin;
        struct sockaddr_in *dst = &fwspec->dst.sin;

        saf = AF_INET;

        src->sin_family = saf;
#if HAVE_SA_LEN
        src->sin_len = sizeof(*src);
#endif
        src->sin_port = htons(src_port);
        src_addr = &src->sin_addr;

        dst->sin_family = saf;
#if HAVE_SA_LEN
        dst->sin_len = sizeof(*dst);
#endif
        dst->sin_port = htons(dst_port);
        dst_addr = &dst->sin_addr;
    }
    else { /* PF_INET6 */
        struct sockaddr_in6 *src = &fwspec->src.sin6;
        struct sockaddr_in6 *dst = &fwspec->dst.sin6;

        saf = AF_INET6;

        src->sin6_family = saf;
#if HAVE_SA_LEN
        src->sin6_len = sizeof(*src);
#endif
        src->sin6_port = htons(src_port);
        src_addr = &src->sin6_addr;

        dst->sin6_family = saf;
#if HAVE_SA_LEN
        dst->sin6_len = sizeof(*dst);
#endif
        dst->sin6_port = htons(dst_port);
        dst_addr = &dst->sin6_addr;
    }

    status = inet_pton(saf, src_addr_str, src_addr);
    LWIP_ASSERT1(status >= 0);
    if (status == 0) {
        DPRINTF(("bad address: %s\n", src_addr_str));
        return -1;
    }

    status = inet_pton(saf, dst_addr_str, dst_addr);
    LWIP_ASSERT1(status >= 0);
    if (status == 0) {
        DPRINTF(("bad address: %s\n", dst_addr_str));
        return -1;
    }

    return 0;
}


int
fwspec_equal(struct fwspec *a, struct fwspec *b)
{
    LWIP_ASSERT1(a != NULL);
    LWIP_ASSERT1(b != NULL);

    if (a->sdom != b->sdom || a->stype != b->stype) {
        return 0;
    }

    if (a->sdom == PF_INET) {
        return a->src.sin.sin_port == b->src.sin.sin_port
            && a->dst.sin.sin_port == b->dst.sin.sin_port
            && a->src.sin.sin_addr.s_addr == b->src.sin.sin_addr.s_addr
            && a->dst.sin.sin_addr.s_addr == b->dst.sin.sin_addr.s_addr;
    }
    else { /* PF_INET6 */
        return a->src.sin6.sin6_port == b->src.sin6.sin6_port
            && a->dst.sin6.sin6_port == b->dst.sin6.sin6_port
            && IN6_ARE_ADDR_EQUAL(&a->src.sin6.sin6_addr, &b->src.sin6.sin6_addr)
            && IN6_ARE_ADDR_EQUAL(&a->dst.sin6.sin6_addr, &b->dst.sin6.sin6_addr);
    }
}


/**
 * Set fwdsrc to the IP address of the peer.
 *
 * For port-forwarded connections originating from hosts loopback the
 * source address is set to the address of one of lwIP interfaces.
 *
 * Currently we only have one interface so there's not much logic
 * here.  In the future we might need to additionally consult fwspec
 * and routing table to determine which netif is used for connections
 * to the specified guest.
 */
int
fwany_ipX_addr_set_src(ipX_addr_t *fwdsrc, const struct sockaddr *peer)
{
    int mapping;

    if (peer->sa_family == AF_INET) {
        const struct sockaddr_in *peer4 = (const struct sockaddr_in *)peer;
        ip_addr_t peerip4;

        peerip4.addr = peer4->sin_addr.s_addr;
        mapping = pxremap_inbound_ip4(&fwdsrc->ip4, &peerip4);
    }
    else if (peer->sa_family == AF_INET6) {
        const struct sockaddr_in6 *peer6 = (const struct sockaddr_in6 *)peer;
        ip6_addr_t peerip6;

        memcpy(&peerip6, &peer6->sin6_addr, sizeof(ip6_addr_t));
        mapping = pxremap_inbound_ip6(&fwdsrc->ip6, &peerip6);
    }
    else {
        mapping = PXREMAP_FAILED;
    }

    return mapping;
}

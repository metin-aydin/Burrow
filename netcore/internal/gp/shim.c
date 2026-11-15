#include "shim.h"
#include "_cgo_export.h"

#include <netdb.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int validate_peer_cert(void *privdata, const char *reason)
{
    // Called only when the system trust store rejected the server certificate: never accept.
    goGPCertRejected((uintptr_t)privdata, (char *)reason);
    return 1;
}

static int process_auth_form(void *privdata, struct oc_auth_form *form)
{
    return goGPAuthForm((uintptr_t)privdata, form);
}

static void progress(void *privdata, int level, const char *fmt, ...)
{
    char buf[1024];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    goGPProgress((uintptr_t)privdata, level, buf);
}

static int getaddrinfo_cb(void *privdata, const char *node, const char *service,
                          const struct addrinfo *hints, struct addrinfo **res)
{
    // System DNS first; if a split-DNS resolver hides the VPN server, Go asks public DNS.
    int rc = getaddrinfo(node, service, hints, res);
    if (rc == 0)
        return 0;
    char *ip = goGPResolve((uintptr_t)privdata, (char *)node);
    if (!ip)
        return rc;
    rc = getaddrinfo(ip, service, hints, res);
    free(ip);
    return rc;
}

struct openconnect_info *burrow_oc_new(uintptr_t handle)
{
    struct openconnect_info *v = openconnect_vpninfo_new("PAN GlobalProtect", validate_peer_cert, NULL,
                                                         process_auth_form, progress, (void *)handle);
    if (v)
        openconnect_override_getaddrinfo(v, getaddrinfo_cb);
    return v;
}

struct oc_form_opt *burrow_oc_opt_next(struct oc_form_opt *opt) { return opt ? opt->next : NULL; }

int burrow_oc_select_count(struct oc_form_opt *opt)
{
    return opt && opt->type == OC_FORM_OPT_SELECT ? ((struct oc_form_opt_select *)opt)->nr_choices : 0;
}

const char *burrow_oc_select_choice(struct oc_form_opt *opt, int i)
{
    struct oc_form_opt_select *s = (struct oc_form_opt_select *)opt;
    return (i >= 0 && i < s->nr_choices) ? s->choices[i]->name : NULL;
}

const char *burrow_oc_select_label(struct oc_form_opt *opt, int i)
{
    struct oc_form_opt_select *s = (struct oc_form_opt_select *)opt;
    return (i >= 0 && i < s->nr_choices) ? s->choices[i]->label : NULL;
}

int burrow_oc_ip_info(struct openconnect_info *vpninfo, const char **addr, const char **addr6,
                      const char **dns0, const char **dns1, const char **dns2, int *mtu)
{
    const struct oc_ip_info *info = NULL;
    if (openconnect_get_ip_info(vpninfo, &info, NULL, NULL) || !info)
        return -1;
    *addr = info->addr;
    *addr6 = info->addr6;
    *dns0 = info->dns[0];
    *dns1 = info->dns[1];
    *dns2 = info->dns[2];
    *mtu = info->mtu;
    return 0;
}

void burrow_oc_cancel(int cmd_fd)
{
    char c = OC_CMD_CANCEL;
    if (write(cmd_fd, &c, 1) < 0) {
        /* nothing to do */
    }
}

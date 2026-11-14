#ifndef BURROW_GP_SHIM_H
#define BURROW_GP_SHIM_H

#include <openconnect.h>
#include <stdint.h>

// Creates an openconnect instance whose callbacks route to Go (handle identifies the Go tunnel).
struct openconnect_info *burrow_oc_new(uintptr_t handle);

// Form helpers (Go can't walk the C linked lists conveniently).
struct oc_form_opt *burrow_oc_opt_next(struct oc_form_opt *opt);
int burrow_oc_select_count(struct oc_form_opt *opt);
const char *burrow_oc_select_choice(struct oc_form_opt *opt, int i);
const char *burrow_oc_select_label(struct oc_form_opt *opt, int i);
int burrow_oc_ip_info(struct openconnect_info *vpninfo, const char **addr, const char **addr6,
                      const char **dns0, const char **dns1, const char **dns2, int *mtu);
void burrow_oc_cancel(int cmd_fd);

#endif

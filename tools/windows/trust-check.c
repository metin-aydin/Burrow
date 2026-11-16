/* Checks that the GnuTLS packaged with Burrow reads Windows' certificate store, which is what openconnect trusts
 * for the VPN server's certificate (ADR 0010). Prints the number of trusted certificates; fails on none. */
#include <gnutls/gnutls.h>
#include <stdio.h>

int main(void)
{
    gnutls_certificate_credentials_t cred;
    if (gnutls_global_init() < 0 || gnutls_certificate_allocate_credentials(&cred) < 0)
        return 2;
    int n = gnutls_certificate_set_x509_system_trust(cred);
    printf("System trust: %d certificates\n", n);
    gnutls_certificate_free_credentials(cred);
    gnutls_global_deinit();
    return n > 0 ? 0 : 1;
}

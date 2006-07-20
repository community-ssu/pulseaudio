/* $Id$ */

#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/ip.h>
#include <stdio.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <assert.h>
#include <string.h>

#include <pulsecore/ipacl.h>

int main(int argc, char *argv[]) {
    struct sockaddr_in sa;
    struct sockaddr_in6 sa6;
    int fd;
    int r;
    pa_ip_acl *acl;

    fd = socket(PF_INET, SOCK_STREAM, 0); 
    assert(fd >= 0);
    
    sa.sin_family = AF_INET;
    sa.sin_port = htons(22);
    sa.sin_addr.s_addr = inet_addr("127.0.0.1");
        
    r = connect(fd, (struct sockaddr*) &sa, sizeof(sa));
    assert(r >= 0);

    acl = pa_ip_acl_new("127.0.0.1");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("127.0.0.2/0");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("127.0.0.1/32");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("127.0.0.1/7");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);
    
    acl = pa_ip_acl_new("127.0.0.2");
    assert(acl);
    printf("result=%u (should be 0)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("127.0.0.0/8;0.0.0.0/32");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("128.0.0.2/9");
    assert(acl);
    printf("result=%u (should be 0)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("::1/9");
    assert(acl);
    printf("result=%u (should be 0)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);
    
    close(fd);

    fd = socket(PF_INET6, SOCK_STREAM, 0);
    assert(fd >= 0);

    memset(&sa6, 0, sizeof(sa6));
    sa6.sin6_family = AF_INET6;
    sa6.sin6_port = htons(22);
    inet_pton(AF_INET6, "::1", &sa6.sin6_addr);
        
    r = connect(fd, (struct sockaddr*) &sa6, sizeof(sa6));
    assert(r >= 0);

    acl = pa_ip_acl_new("::1");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("::1/9");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("::/0");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("::2/128");
    assert(acl);
    printf("result=%u (should be 0)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("::2/127");
    assert(acl);
    printf("result=%u (should be 0)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    acl = pa_ip_acl_new("::2/126");
    assert(acl);
    printf("result=%u (should be 1)\n", pa_ip_acl_check(acl, fd));
    pa_ip_acl_free(acl);

    close(fd);
    
    return 0;
}
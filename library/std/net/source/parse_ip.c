#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetIpAddressResult r_std_net_parse_ip(RStdStringView text) {
    return r_library_internal_net_parse_ip(text);
}

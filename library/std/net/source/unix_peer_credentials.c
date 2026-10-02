#include "r_std_net.h"

#include "r_library_net_internal.h"

RStdNetPeerCredentialsResult r_std_net_unix_peer_credentials(const RStdNetUnixStream *stream) {
    return r_library_internal_net_unix_peer_credentials(stream);
}

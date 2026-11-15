#pragma once
#define TB_CORE_NETWORK_BUILD_CONTRACT "TBX_CORE_IPV4_FORWARD_NAPT64_V1"
#if defined(CONFIG_TB_ROLE_CORE) && CONFIG_TB_ROLE_CORE
#if !defined(CONFIG_LWIP_IP_FORWARD) || !CONFIG_LWIP_IP_FORWARD || !defined(CONFIG_LWIP_IPV4_NAPT) || !CONFIG_LWIP_IPV4_NAPT
#error "Core internet sharing requires actual IPv4 forwarding and NAPT, not defaults alone"
#endif
#endif

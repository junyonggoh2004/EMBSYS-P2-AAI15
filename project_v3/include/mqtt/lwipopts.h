#ifndef LWIPOPTS_H
#define LWIPOPTS_H

// -----------------------------------------------------------------------------
// Core mode: Pico W background arch runs lwIP with NO_SYS
// -----------------------------------------------------------------------------
#define NO_SYS                           1
#define NO_SYS_NO_TIMERS                 0   // background arch provides timers

// -----------------------------------------------------------------------------
// Disable sequential API (requires NO_SYS=0), keep only raw API
// -----------------------------------------------------------------------------
#define LWIP_NETCONN                     0
#define LWIP_SOCKET                      0

// -----------------------------------------------------------------------------
// Protocols
// -----------------------------------------------------------------------------
#define LWIP_TCP                         1
#define LWIP_UDP                         1
#define LWIP_ICMP                        1
#define LWIP_RAW                         1

#define LWIP_IPV4                        1
#define LWIP_IPV6                        0

// DHCP/DNS for Wi-Fi networking and broker name resolution
#define LWIP_DHCP                        1
#define LWIP_DNS                         1

// -----------------------------------------------------------------------------
// Memory (tune if you see ENOMEM, but these are safe starters)
// -----------------------------------------------------------------------------
#define MEM_ALIGNMENT                    4
#define MEM_SIZE                         40000

// pbuf pool
#define PBUF_POOL_SIZE                   16

// TCP tuning
#define TCP_MSS                          1460
#define TCP_SND_BUF                      (4 * TCP_MSS)
#define TCP_WND                          (4 * TCP_MSS)

// Timeouts used by DNS/MQTT/etc.
#define MEMP_NUM_SYS_TIMEOUT             16

// -----------------------------------------------------------------------------
// Helpers / miscellany
// -----------------------------------------------------------------------------
#define LWIP_NETIF_STATUS_CALLBACK       1
#define LWIP_NETIF_LINK_CALLBACK         1
#define LWIP_NETIF_HOSTNAME              1
#define LWIP_NETIF_TX_SINGLE_PBUF        1

#define LWIP_STATS                       0
#define LWIP_PROVIDE_ERRNO               1

// -----------------------------------------------------------------------------
// Enable the lwIP MQTT client
// -----------------------------------------------------------------------------
#define LWIP_MQTT                        1

#endif // LWIPOPTS_H

// web_net.cpp - win32/win_net.cpp for the web build: the engine's UDP packets go through two shared-memory rings to
// the page, which carries them over WebRTC data channels or the lobby relay (docs/web-engine-interface.md section 3).
//
// Every player is 10.66.0.<lobby slot + 1>:28960. NET_OpenIP sets net_ip to the address the page put in local_ip and
// net_port to 28960. Sys_SendPacket writes to_page and wakes the page (Atomics.notify on write_index); Sys_GetPacket
// polls from_page. Both are safe from several engine threads (the server thread and the main thread both send and
// receive): each direction has its own lock, and each ring still has exactly one producer and one consumer.
// Broadcast packets (LAN server discovery) are dropped: the page has no broadcast route.
// The remote debug socket (Sys_*DebugSocket*, net_listen / net_connect) does not exist on the web.
//
// native (BO1_NATIVE): a real UDP socket instead, as win32/win_net.cpp has it: bound to net_port (the next free one
// of ten when taken) on net_ip or every interface, non-blocking, broadcast allowed, names resolved through DNS.
// Builds of this engine on any platform then reach each other by address (+connect <ip>:<port>).
#include <win32/win_net.h>
#include <qcommon/net_chan_mp.h>
#include <qcommon/msg_mp.h>
#include <qcommon/common.h>
#include <universal/dvar.h>
#include <universal/q_shared.h>

#include "web_bridge.h"

#include "bo1_platform.h"
#include <arpa/inet.h>
#include <pthread.h>
#ifdef BO1_NATIVE
#include <errno.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

const dvar_t *ip;
const dvar_t *port;
const dvar_t *net_noudp;
const dvar_t *net_socksEnabled;
const dvar_t *net_socksServer;
const dvar_t *net_socksPort;
const dvar_t *net_socksUsername;
const dvar_t *net_socksPassword;

int numIP;
unsigned __int8 localIP[16][4];
int networkingEnabled;
int g_debugClient;
int g_debugServer;

namespace
{
struct alignas(16) RingStorage
{
    bo1_net_ring header;
    bo1_net_slot slots[BO1_NET_RING_CAPACITY];
};
static_assert(offsetof(RingStorage, slots) == 16, "ring slots start at offset 16");

RingStorage s_toPage;
RingStorage s_fromPage;
bo1_net_shared s_shared;
pthread_mutex_t s_sendLock = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t s_recvLock = PTHREAD_MUTEX_INITIALIZER;
bool s_open;   // "socket" open (NET_OpenIP .. NET_Config(0))
#ifdef BO1_NATIVE
int s_socket = -1;

// win_net.cpp NET_IPSocket: a non-blocking UDP socket bound to the interface ("localhost": all) and port; -1 on failure
int OpenUdpSocket(const char *netInterface, int portNumber)
{
    const int fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (fd < 0)
    {
        Com_Printf(16, "WARNING: UDP_OpenSocket: socket: %s\n", strerror(errno));
        return -1;
    }
    fcntl(fd, F_SETFL, fcntl(fd, F_GETFL, 0) | O_NONBLOCK);
    int on = 1;
    setsockopt(fd, SOL_SOCKET, SO_BROADCAST, &on, sizeof(on));
    sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    if (netInterface && *netInterface && I_stricmp(netInterface, "localhost"))
    {
        sockaddr named;
        if (Sys_StringToSockaddr(netInterface, &named))
            address.sin_addr = ((sockaddr_in *)&named)->sin_addr;
    }
    address.sin_port = htons((unsigned short)portNumber);
    if (bind(fd, (sockaddr *)&address, sizeof(address)) != 0)
    {
        close(fd);
        return -1;
    }
    return fd;
}

// the machine's own IPv4 addresses, for Sys_IsLANAddress and Sys_ShowIP
void ReadLocalAddresses()
{
    numIP = 0;
    ifaddrs *list = nullptr;
    if (getifaddrs(&list) != 0)
        return;
    for (ifaddrs *i = list; i && numIP < 16; i = i->ifa_next)
    {
        if (!i->ifa_addr || i->ifa_addr->sa_family != AF_INET || (i->ifa_flags & IFF_LOOPBACK))
            continue;
        memcpy(localIP[numIP], &((sockaddr_in *)i->ifa_addr)->sin_addr.s_addr, 4);
        ++numIP;
    }
    freeifaddrs(list);
}
#endif

void InitRings()
{
    if (s_shared.ready)
        return;
    s_toPage.header.capacity = BO1_NET_RING_CAPACITY;
    s_toPage.header.slot_size = BO1_NET_SLOT_SIZE;
    s_fromPage.header.capacity = BO1_NET_RING_CAPACITY;
    s_fromPage.header.slot_size = BO1_NET_SLOT_SIZE;
    s_shared.to_page = &s_toPage.header;
    s_shared.from_page = &s_fromPage.header;
    __atomic_store_n(&s_shared.ready, 1u, __ATOMIC_RELEASE);
}

uint32_t PackIp(const unsigned __int8 *b)
{
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) | ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}
}

extern "C" EMSCRIPTEN_KEEPALIVE bo1_net_shared *bo1_net_get_shared(void)
{
    InitRings();
    return &s_shared;
}

void __cdecl NET_Sleep(unsigned int msec)
{
#ifdef BO1_NATIVE
    Sleep(msec);   // as win_net.cpp
    return;
#endif
    // wait for the page to write a packet (or the time to pass)
    const uint32_t seen = s_fromPage.header.write_index.load(std::memory_order_acquire);
    if (s_fromPage.header.read_index.load(std::memory_order_relaxed) != seen)
        return;
    bo1_plat_futex_wait((volatile void *)&s_fromPage.header.write_index, seen, (double)msec);
}

const char *__cdecl NET_ErrorString()
{
    return "NO ERROR";
}

int __cdecl Sys_StringToSockaddr(const char *s, sockaddr *sadr)
{
    memset(sadr, 0, sizeof(*sadr));
    sadr->sa_family = AF_INET;
    sockaddr_in *in = (sockaddr_in *)sadr;
    if (I_isdigit(*s))
    {
        in->sin_addr.s_addr = inet_addr(s);
        return 1;
    }
    if (!I_stricmp(s, "localhost"))
    {
        in->sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        return 1;
    }
#ifdef BO1_NATIVE
    addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo *found = nullptr;
    if (getaddrinfo(s, nullptr, &hints, &found) != 0 || !found)
        return 0;
    in->sin_addr = ((sockaddr_in *)found->ai_addr)->sin_addr;
    freeaddrinfo(found);
    return 1;
#else
    return 0;   // web: no DNS
#endif
}

void __cdecl SockadrToNetadr(sockaddr *s, netadr_t *a)
{
    if (s->sa_family == AF_INET)
    {
        const sockaddr_in *in = (const sockaddr_in *)s;
        a->type = NA_IP;
        memcpy(a->ip, &in->sin_addr.s_addr, 4);
        a->port = in->sin_port;
    }
}

int __cdecl Sys_StringToAdr(const char *s, netadr_t *a)
{
    sockaddr sadr;
    if (!Sys_StringToSockaddr(s, &sadr))
        return 0;
    SockadrToNetadr(&sadr, a);
    return 1;
}

int __cdecl Sys_GetPacket(netadr_t *net_from, msg_t *net_message)
{
    if (!s_open)
        return 0;
#ifdef BO1_NATIVE
    for (;;)
    {
        if (s_socket < 0)
            return 0;
        sockaddr_in from;
        socklen_t fromLength = sizeof(from);
        const ssize_t received = recvfrom(s_socket, net_message->data, (size_t)net_message->maxsize, 0, (sockaddr *)&from,
            &fromLength);
        if (received < 0)
            return 0;   // nothing waiting (or a refused datagram's ICMP answer: ignored as on Windows)
        memset(net_from, 0, sizeof(*net_from));
        SockadrToNetadr((sockaddr *)&from, net_from);
        if ((int)received >= net_message->maxsize)
        {
            Com_Printf(16, "Oversize packet from %s\n", NET_AdrToString(*net_from));
            continue;
        }
        net_message->cursize = (int)received;
        net_message->readcount = 0;
        return 1;
    }
#endif
    bo1_net_ring &ring = s_fromPage.header;
    pthread_mutex_lock(&s_recvLock);
    for (;;)
    {
        const uint32_t r = ring.read_index.load(std::memory_order_relaxed);
        const uint32_t w = ring.write_index.load(std::memory_order_acquire);
        if (r == w)
        {
            pthread_mutex_unlock(&s_recvLock);
            return 0;
        }
        const bo1_net_slot &slot = s_fromPage.slots[r & (BO1_NET_RING_CAPACITY - 1)];
        const unsigned int length = slot.length;
        memset(net_from, 0, sizeof(*net_from));
        net_from->type = NA_IP;
        net_from->ip[0] = (unsigned __int8)(slot.ip);
        net_from->ip[1] = (unsigned __int8)(slot.ip >> 8);
        net_from->ip[2] = (unsigned __int8)(slot.ip >> 16);
        net_from->ip[3] = (unsigned __int8)(slot.ip >> 24);
        net_from->port = htons(slot.port);
        const bool fits = length <= BO1_NET_MAX_PACKET && (int)length < net_message->maxsize;
        if (fits)
        {
            memcpy(net_message->data, slot.data, length);
            net_message->cursize = (int)length;
            net_message->readcount = 0;
        }
        ring.read_index.store(r + 1, std::memory_order_release);
        if (fits)
        {
            pthread_mutex_unlock(&s_recvLock);
            return 1;
        }
        Com_Printf(16, "Oversize packet from %s\n", NET_AdrToString(*net_from));
    }
}

char __cdecl Sys_SendPacket(unsigned int length, unsigned __int8 *data, netadr_t to)
{
    switch (to.type)
    {
    case NA_IP:
        break;
    case NA_BROADCAST:
#ifdef BO1_NATIVE
        break;
#else
        return 1;   // no broadcast route on the web
#endif
    default:
        Com_Error(ERR_FATAL, "Sys_SendPacket: bad address type");
        return 0;
    }
    if (!s_open)
        return 1;
#ifdef BO1_NATIVE
    {
        if (s_socket < 0)
            return 1;
        sockaddr_in address;
        memset(&address, 0, sizeof(address));
        address.sin_family = AF_INET;
        address.sin_port = to.port;
        if (to.type == NA_BROADCAST)
            address.sin_addr.s_addr = htonl(INADDR_BROADCAST);
        else
            memcpy(&address.sin_addr.s_addr, to.ip, 4);
        if (sendto(s_socket, data, length, 0, (sockaddr *)&address, sizeof(address)) < 0)
        {
            // a full send buffer or an unreachable broadcast is a lost datagram, not an error
            if (errno != EAGAIN && errno != EWOULDBLOCK && to.type != NA_BROADCAST)
                Com_PrintError(16, "Sys_SendPacket: %s\n", strerror(errno));
            return 0;
        }
        return 1;
    }
#endif
    if (length > BO1_NET_MAX_PACKET)
    {
        Com_PrintError(16, "Sys_SendPacket: %u-byte packet is larger than the web transport's %d\n", length, BO1_NET_MAX_PACKET);
        return 0;
    }
    bo1_net_ring &ring = s_toPage.header;
    pthread_mutex_lock(&s_sendLock);
    const uint32_t w = ring.write_index.load(std::memory_order_relaxed);
    const uint32_t r = ring.read_index.load(std::memory_order_acquire);
    if (w - r >= BO1_NET_RING_CAPACITY)
    {
        pthread_mutex_unlock(&s_sendLock);
        return 1;   // full: dropped (UDP semantics; the netchan retransmits what matters)
    }
    bo1_net_slot &slot = s_toPage.slots[w & (BO1_NET_RING_CAPACITY - 1)];
    slot.ip = PackIp(to.ip);
    slot.port = ntohs(to.port);
    slot.length = (uint16_t)length;
    memcpy(slot.data, data, length);
    ring.write_index.store(w + 1, std::memory_order_release);
    pthread_mutex_unlock(&s_sendLock);
    bo1_plat_futex_wake_all((volatile void *)&ring.write_index);
    return 1;
}

bool __cdecl Sys_IsLANAddress_IgnoreSubnet(netadr_t adr)
{
    if (adr.type == NA_LOOPBACK)
        return 1;
    if (adr.type == NA_BOT)
        return 1;
    if (adr.type != NA_IP)
        return 0;
    if (adr.ip[0] == 10)
        return 1;
    if (adr.ip[0] == 127)
        return 1;
    if (adr.ip[0] == 169 && adr.ip[1] == 254)
        return 1;
    if (adr.ip[0] == 172 && (adr.ip[1] & 0xF0) == 0x10)
        return 1;
    return adr.ip[0] == 192 && adr.ip[1] == 168;
}

int __cdecl Sys_IsLANAddress(netadr_t adr)
{
    if (Sys_IsLANAddress_IgnoreSubnet(adr))
        return 1;
    for (int i = 0; i < numIP; ++i)
    {
        if (adr.ip[0] == localIP[i][0] && adr.ip[1] == localIP[i][1] && adr.ip[2] == localIP[i][2])
            return 1;
    }
    return 0;
}

void __cdecl Sys_ShowIP()
{
    for (int i = 0; i < numIP; ++i)
        Com_Printf(16, "IP: %i.%i.%i.%i\n", localIP[i][0], localIP[i][1], localIP[i][2], localIP[i][3]);
}

void __cdecl NET_OpenIP()
{
    ip = _Dvar_RegisterString("net_ip", "localhost", 0x20u, "Network IP Address");
    port = _Dvar_RegisterInt("net_port", 3074, 0, 0xFFFF, 0x20u, "Network port");
#ifdef BO1_NATIVE
    {
        if (s_socket >= 0)
        {
            close(s_socket);
            s_socket = -1;
        }
        // the first free of ten ports from net_port (a client on the server's machine takes the next one)
        const int wanted = port->current.integer;
        for (int i = 0; i < 10 && s_socket < 0; ++i)
        {
            s_socket = OpenUdpSocket(ip->current.string, wanted + i);
            if (s_socket >= 0)
            {
                Dvar_SetInt((dvar_s *)port, wanted + i);
                Com_Printf(16, "Opening IP socket: %s:%i\n", ip->current.string, wanted + i);
            }
        }
        if (s_socket < 0)
            Com_PrintWarning(16, "WARNING: Couldn't allocate IP port %i..%i: network play is off\n", wanted, wanted + 9);
        ReadLocalAddresses();
        Sys_ShowIP();
        s_open = s_socket >= 0;
        return;
    }
#endif
    InitRings();
    const uint32_t local = __atomic_load_n(&s_shared.local_ip, __ATOMIC_ACQUIRE);
    if (local)
    {
        localIP[0][0] = (unsigned __int8)local;
        localIP[0][1] = (unsigned __int8)(local >> 8);
        localIP[0][2] = (unsigned __int8)(local >> 16);
        localIP[0][3] = (unsigned __int8)(local >> 24);
        numIP = 1;
        Dvar_SetString((dvar_s *)ip, va("%i.%i.%i.%i", localIP[0][0], localIP[0][1], localIP[0][2], localIP[0][3]));
    }
    else
    {
        numIP = 0;
        Com_PrintWarning(16, "WARNING: the page set no local address (bo1_net_shared.local_ip); network play is off\n");
    }
    Dvar_SetInt((dvar_s *)port, BO1_NET_GAME_PORT);
    s_open = true;
    Com_Printf(16, "Network: web rings, address %s:%i\n", ip->current.string, port->current.integer);
}

void __cdecl NET_SocketPool_Init()
{
}

void __cdecl Sys_CheckForNATOverflow()
{
}

int __cdecl Sys_SocketPool_GetPacket(netadr_t *net_from, msg_t *net_message)
{
    return 0;
}

bool __cdecl NET_GetDvars()
{
    bool modified = net_noudp && net_noudp->modified;
    net_noudp = _Dvar_RegisterBool("net_noudp", 0, 0x21u, "Disable UDP");
    if (net_socksEnabled && net_socksEnabled->modified)
        modified = 1;
    net_socksEnabled = _Dvar_RegisterBool("net_socksEnabled", 0, 0x21u, "Enable network sockets");
    if (net_socksServer && net_socksServer->modified)
        modified = 1;
    net_socksServer = _Dvar_RegisterString("net_socksServer", (char *)"", 0x21u, "Network socket server");
    if (net_socksPort && net_socksPort->modified)
        modified = 1;
    net_socksPort = _Dvar_RegisterInt("net_socksPort", 1080, 0, 0xFFFF, 0x21u, "Network socket port");
    if (net_socksUsername && net_socksUsername->modified)
        modified = 1;
    net_socksUsername = _Dvar_RegisterString("net_socksUsername", (char *)"", 0x21u, "Network socket username");
    if (net_socksPassword && net_socksPassword->modified)
        modified = 1;
    net_socksPassword = _Dvar_RegisterString("net_socksPassword", (char *)"", 0x21u, "Network socket password");
    return modified;
}

void __cdecl NET_Config(int enableNetworking)
{
    const bool modified = NET_GetDvars();
    if (net_noudp->current.enabled)
        enableNetworking = 0;
    if (enableNetworking == networkingEnabled && !modified)
        return;
    bool stop, start;
    if (enableNetworking == networkingEnabled)
    {
        stop = start = enableNetworking != 0;
    }
    else
    {
        stop = !enableNetworking;
        start = enableNetworking != 0;
        networkingEnabled = enableNetworking;
    }
    if (stop)
    {
        s_open = false;
#ifdef BO1_NATIVE
        if (s_socket >= 0)
        {
            close(s_socket);
            s_socket = -1;
        }
#endif
    }
    if (start && !net_noudp->current.enabled)
    {
        NET_OpenIP();
        NET_SocketPool_Init();
    }
}

void __cdecl NET_Restart()
{
    NET_Config(networkingEnabled);
}

void __cdecl NET_InitDebug()
{
    g_debugClient = 0;
    g_debugServer = 0;
}

int NET_InitDebugStreams()
{
    return 0;
}

void __cdecl NET_Init()
{
    InitRings();
    Com_Printf(16, "Network: web transport (docs/web-engine-interface.md)\n");
    NET_GetDvars();
    NET_Config(1);
    NET_InitDebug();
}

// ----- remote debug socket: not on the web -----
void __cdecl Sys_Listen_f()
{
    Com_Printf(16, "net_listen: no debug sockets in the browser\n");
}

void __cdecl Sys_Connect_f()
{
    Com_Printf(16, "net_connect: no debug sockets in the browser\n");
}

unsigned int __cdecl NET_TCPIPSocket(char *net_interface, int port, int type)
{
    return 0;
}

int __cdecl NET_Select(unsigned int socket)
{
    return 0;
}

void __cdecl Sys_DebugSocketError(const char *message)
{
}

int __cdecl Sys_IsRemoteDebugServer()
{
    return 0;
}

char __cdecl Sys_StartRemoteDebugServer()
{
    return 0;
}

int __cdecl Sys_IsRemoteDebugClient()
{
    return 0;
}

void __cdecl NET_ShutdownDebug()
{
}

void __cdecl NET_RestartDebug()
{
}

int __cdecl Sys_ReadDebugSocketData(char *buffer, int len, int blocking)
{
    return 0;
}

void Sys_SendDebugReadBytesInternal()
{
}

void __cdecl Sys_SendDebugReadBytes(int read)
{
}

int __cdecl Sys_ReadDebugSocketInt()
{
    return 0;
}

char *__cdecl Sys_ReadDebugSocketString()
{
    static char empty[1];
    return empty;
}

void __cdecl Sys_WriteDebugSocketData(unsigned __int8 *buffer, int len)
{
}

void __cdecl Sys_DebugSend(int channel, const char *buf, int len, const char *name)
{
}

bool __cdecl Sys_DebugCanSend()
{
    return false;
}

void __cdecl Sys_WriteDebugSocketInt(int value)
{
}

void __cdecl Sys_FlushDebugSocketData()
{
}

bool __cdecl Sys_DebugSocketReady(int channel)
{
    return false;
}

void __cdecl Sys_WriteDebugSocketMessageType(unsigned __int8 type)
{
}

void __cdecl Sys_EndWriteDebugSocket()
{
}

void __cdecl Sys_WriteDebugSocketString(char *text)
{
}

int __cdecl Sys_UpdateDebugSocket()
{
    return 0;
}

int __cdecl Sys_ReadDebugSocketMessageType(unsigned __int8 *type, int blocking)
{
    return 0;
}

void __cdecl Sys_AckDebugSocket()
{
}

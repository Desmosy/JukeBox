// DHCP Library
// Jason Losh
// Koshish Shrestha Work 
//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: -
// Target uC:       -
// System Clock:    -

// Hardware configuration:
// -

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#include <stdio.h>
#include <string.h>
#include "dhcp.h"
#include "arp.h"
#include "timer.h"
#include "eth0.h"
#include "ip.h"
#include "udp.h"
#include "uart0.h"

#define DHCPDISCOVER 1
#define DHCPOFFER    2
#define DHCPREQUEST  3
#define DHCPDECLINE  4
#define DHCPACK      5
#define DHCPNAK      6
#define DHCPRELEASE  7
#define DHCPINFORM   8

#define DHCP_DISABLED   0
#define DHCP_INIT       1
#define DHCP_SELECTING  2
#define DHCP_REQUESTING 3
#define DHCP_TESTING_IP 4
#define DHCP_BOUND      5
#define DHCP_RENEWING   6
#define DHCP_REBINDING  7
#define DHCP_INITREBOOT 8 // not used since ip not stored over reboot
#define DHCP_REBOOTING  9 // not used since ip not stored over reboot
#define DHCP_SERVER_PORT 67 // dhcp server listens on 
#define DHCP_CLIENT_PORT 68 //  dhcp client listesn on 
#define DHCP_MAGIC_COOKIE 0x63825363 // magic cookie for dhcp to sep from bootp
#define DHCP_TEST_LEASE_30 1

// dhcp options coe
static void handleNak(void);
static void setBound(void);
static void stopTimers(void);

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

uint32_t xid = 0;
uint32_t leaseSeconds = 0;
uint32_t leaseT1 = 0;
uint32_t leaseT2 = 0;

// use these variables if you want
bool discoverNeeded = false;
bool requestNeeded = false;
bool releaseNeeded = false;

bool ipConflictDetectionMode = false;

uint8_t dhcpOfferedIpAdd[4];
uint8_t dhcpServerIpAdd[4];

uint8_t dhcpState = DHCP_DISABLED;
bool    dhcpEnabled = true;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// State functions

void setDhcpState(uint8_t state)
{
    dhcpState = state;
}

uint8_t getDhcpState()
{
    return dhcpState;
}

// New address functions
// Manually requested at start-up
// Discover messages sent every 15 seconds

void callbackDhcpGetNewAddressTimer()
{
    bool shouldRetryDiscover = (dhcpState == DHCP_INIT) | (dhcpState == DHCP_SELECTING);
    if (shouldRetryDiscover)
        discoverNeeded = true;
}

void requestDhcpNewAddress()
{
    discoverNeeded = true;
    xid = random32();
    uint8_t noAddress[IP_ADD_LENGTH];
    memset(noAddress, 0, IP_ADD_LENGTH);
    startPeriodicTimer(callbackDhcpGetNewAddressTimer, 15);
    setDhcpState(DHCP_INIT);
    setIpAddress(noAddress);
}

// Renew functions

void renewDhcp()
{
    if (dhcpState == DHCP_BOUND || dhcpState == DHCP_RENEWING || dhcpState == DHCP_REBINDING)
    {
        requestNeeded = true;
        setDhcpState(DHCP_RENEWING);
    }
    else
    {
        requestDhcpNewAddress();
    }
}

void callbackDhcpT1PeriodicTimer()
{
    if (dhcpState != DHCP_RENEWING)
        return;
    requestNeeded = true;
}

void callbackDhcpT1HitTimer()
{
    renewDhcp();
    startPeriodicTimer(callbackDhcpT1PeriodicTimer, 15);
}

// Rebind functions

void rebindDhcp()
{
    stopTimer(callbackDhcpT1PeriodicTimer);
    setDhcpState(DHCP_REBINDING);
    requestNeeded = true;
}

void callbackDhcpT2PeriodicTimer()
{
    if (dhcpState != DHCP_REBINDING)
        return;
    requestNeeded = true;
}

void callbackDhcpT2HitTimer()
{
    rebindDhcp();
    startPeriodicTimer(callbackDhcpT2PeriodicTimer, 15);
}

// End of lease timer
void callbackDhcpLeaseEndTimer()
{
    handleNak();
}

// Release functions

void releaseDhcp()
{
    stopTimers();
    stopTimer(callbackDhcpGetNewAddressTimer);
    releaseNeeded = true;
    uint8_t clearedAddr[IP_ADD_LENGTH];
    memset(clearedAddr, 0, IP_ADD_LENGTH);
    setIpAddress(clearedAddr);
    setDhcpState(DHCP_INIT);
    leaseSeconds = 0;
    leaseT1 = 0;
    leaseT2 = 0;
}

// IP conflict detection

void callbackDhcpIpConflictWindow()
{
    ipConflictDetectionMode = false;
    setBound();
}

void requestDhcpIpConflictTest()
{
    ipConflictDetectionMode = true;
    startOneshotTimer(callbackDhcpIpConflictWindow, 2);
}

bool isDhcpIpConflictDetectionMode()
{
    return ipConflictDetectionMode;
}

// Lease functions

uint32_t getDhcpLeaseSeconds()
{
    return leaseSeconds;
}

// Determines whether packet is DHCP
// Must be a UDP packet
bool isDhcpResponse(etherHeader* ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ipHeaderLength);
    dhcpFrame *dhcp = (dhcpFrame*)udp->data;
    
    return (ntohs(udp->sourcePort) == DHCP_SERVER_PORT &&
            ntohs(udp->destPort) == DHCP_CLIENT_PORT &&
            dhcp->op == 2 &&
            dhcp->xid == htonl(xid) &&
            dhcp->magicCookie == htonl(DHCP_MAGIC_COOKIE));
}

static uint16_t addOpt(uint8_t *buf, uint16_t p, uint8_t t, uint8_t n, const uint8_t *val)
{
    buf[p++] = t;
    buf[p++] = n;
    memcpy(&buf[p], val, n);
    return p + n;
}

// Send DHCP message
void sendDhcpMessage(etherHeader *ether, uint8_t type)
{
    uint8_t mac[HW_ADD_LENGTH];
    getEtherMacAddress(mac);

    uint8_t buffer[sizeof(dhcpFrame) + 64];
    dhcpFrame *dhcp = (dhcpFrame *)buffer;
    memset(buffer, 0, sizeof(buffer));
    uint16_t p = 0;

    dhcp->op    = 1;
    dhcp->htype = 1;
    dhcp->hlen  = HW_ADD_LENGTH;
    dhcp->xid   = htonl(xid);

    bool unicast = (type == DHCPRELEASE) ||
                   (type == DHCPREQUEST && dhcpState == DHCP_RENEWING);
    dhcp->flags = unicast ? 0 : htons(0x8000);

    bool sendCiaddr = (type == DHCPRELEASE) ||
                      (type == DHCPREQUEST &&
                      (dhcpState == DHCP_RENEWING || dhcpState == DHCP_REBINDING));
    if (sendCiaddr)
        memcpy(dhcp->ciaddr, dhcpOfferedIpAdd, IP_ADD_LENGTH);

    memcpy(dhcp->chaddr, mac, HW_ADD_LENGTH);
    dhcp->magicCookie = htonl(DHCP_MAGIC_COOKIE);

    uint8_t typeArr[1] = { type };
    p = addOpt(dhcp->options, p, 53, 1, typeArr);

    if (type == DHCPREQUEST && dhcpState == DHCP_REQUESTING) {
        p = addOpt(dhcp->options, p, 50, 4, dhcpOfferedIpAdd);
        p = addOpt(dhcp->options, p, 54, 4, dhcpServerIpAdd);
    }
    if (type == DHCPDECLINE) {
        p = addOpt(dhcp->options, p, 50, 4, dhcpOfferedIpAdd);
        p = addOpt(dhcp->options, p, 54, 4, dhcpServerIpAdd);
    }
    if (type == DHCPRELEASE)
        p = addOpt(dhcp->options, p, 54, 4, dhcpServerIpAdd);

    if (type == DHCPDISCOVER || type == DHCPREQUEST) {
        uint8_t prl[3] = {1, 3, 6};
        p = addOpt(dhcp->options, p, 55, 3, prl);
    }

    dhcp->options[p++] = 255;

    socket s;
    s.localPort  = DHCP_CLIENT_PORT;
    s.remotePort = DHCP_SERVER_PORT;
    memset(s.remoteHwAddress, 0xFF, HW_ADD_LENGTH);
    if (unicast)
        memcpy(s.remoteIpAddress, dhcpServerIpAdd, IP_ADD_LENGTH);
    else
        memset(s.remoteIpAddress, 0xFF, IP_ADD_LENGTH);

    sendUdpMessage(ether, s, buffer, sizeof(dhcpFrame) + p);
}

uint8_t* getDhcpOption(etherHeader *ether, uint8_t opt, uint8_t* n)
{
    ipHeader  *ip   = (ipHeader *)ether->data;
    udpHeader *udp  = (udpHeader *)((uint8_t *)ip + ip->size * 4);
    dhcpFrame *dhcp = (dhcpFrame *)udp->data;
    uint16_t i = 0;
    uint16_t payLen = ntohs(udp->length) - sizeof(udpHeader);
    uint16_t optLen = (payLen > 240) ? payLen - 240 : 0;

    while (i < optLen)
    {
        uint8_t t = dhcp->options[i];
        if (t == 255) break;
        if (t == 0)  { i++; continue; }
        uint8_t len = dhcp->options[i + 1];
        if (t == opt) {
            *n = len;
            return &dhcp->options[i + 2];
        }
        i += 2 + len;
    }
    *n = 0;
    return NULL;
}

static bool getOptVal(etherHeader *ether, uint8_t opt, uint8_t *dst, uint8_t n)
{
    uint8_t got;
    uint8_t *p = getDhcpOption(ether, opt, &got);
    if (p == NULL || got < n)
        return false;
    memcpy(dst, p, n);
    return true;
}

// Determines whether packet is DHCP offer response to DHCP discover
// Must be a UDP packet
bool isDhcpOffer(etherHeader *ether, uint8_t ipOfferedAdd[])
{
    uint8_t n;
    uint8_t *p = getDhcpOption(ether, 53, &n);
    if (p == NULL || n != 1 || p[0] != DHCPOFFER)
        return false;

    ipHeader  *ip   = (ipHeader *)ether->data;
    udpHeader *udp  = (udpHeader *)((uint8_t *)ip + ip->size * 4);
    dhcpFrame *dhcp = (dhcpFrame *)udp->data;
    memcpy(ipOfferedAdd, dhcp->yiaddr, IP_ADD_LENGTH);
    return true;
}

// Determines whether packet is DHCP ACK response to DHCP request
// Must be a UDP packet
bool isDhcpAck(etherHeader *ether)
{
    uint8_t n;
    uint8_t *p = getDhcpOption(ether, 53, &n);
    if (p == NULL || n != 1)
        return false;
    return (p[0] == DHCPACK);
}

// Determines whether packet is DHCP NAK response
// Must be a UDP packet
bool isDhcpNak(etherHeader *ether)
{
    uint8_t n;
    uint8_t *p = getDhcpOption(ether, 53, &n);
    if (p == NULL || n != 1)
        return false;
    return (p[0] == DHCPNAK);
}

// Handle a DHCP ACK
void handleDhcpAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    udpHeader *udp = (udpHeader*)((uint8_t*)ip + ip->size * 4);
    dhcpFrame *dhcp = (dhcpFrame*)udp->data;

    uint8_t leaseRaw[4];
    if (getOptVal(ether, 51, leaseRaw, 4))
        leaseSeconds = ntohl(*(uint32_t*)leaseRaw);
    else
        leaseSeconds = 60;
#ifdef DHCP_TEST_LEASE_30
    leaseSeconds = 30;
#endif

    leaseT1 = leaseSeconds >> 1;
    leaseT2 = leaseSeconds - (leaseSeconds >> 3);

    getOptVal(ether, 54, dhcpServerIpAdd, IP_ADD_LENGTH);
    memcpy(dhcpOfferedIpAdd, dhcp->yiaddr, IP_ADD_LENGTH);

    uint8_t subnetValue[IP_ADD_LENGTH];
    if (getOptVal(ether, 1, subnetValue, IP_ADD_LENGTH))
        setIpSubnetMask(subnetValue);

    uint8_t gatewayValue[IP_ADD_LENGTH];
    if (getOptVal(ether, 3, gatewayValue, IP_ADD_LENGTH))
        setIpGatewayAddress(gatewayValue);

    uint8_t dnsValue[IP_ADD_LENGTH];
    if (getOptVal(ether, 6, dnsValue, IP_ADD_LENGTH))
        setIpDnsAddress(dnsValue);
}

// Message requests

bool isDhcpDiscoverNeeded()
{
    return discoverNeeded;
}

bool isDhcpRequestNeeded()
{
    return requestNeeded;
}

bool isDhcpReleaseNeeded()
{
    return releaseNeeded;
}

void sendDhcpPendingMessages(etherHeader *ether)
{
    if (discoverNeeded)
    {
        putsUart0("DHCP: sending DISCOVER\n");
        discoverNeeded = false;
        sendDhcpMessage(ether, DHCPDISCOVER);
        setDhcpState(DHCP_SELECTING);
    }
    else if (requestNeeded)
    {
        putsUart0("DHCP: sending REQUEST\n");
        requestNeeded = false;
        sendDhcpMessage(ether, DHCPREQUEST);
    }
    else if (releaseNeeded)
    {
        putsUart0("DHCP: sending RELEASE\n");
        releaseNeeded = false;
        sendDhcpMessage(ether, DHCPRELEASE);
    }
}

static void stopTimers(void)
{
    stopTimer(callbackDhcpT1HitTimer);
    stopTimer(callbackDhcpT2HitTimer);
    stopTimer(callbackDhcpT1PeriodicTimer);
    stopTimer(callbackDhcpT2PeriodicTimer);
    stopTimer(callbackDhcpLeaseEndTimer);
}

static void handleNak(void)
{
    stopTimers();
    uint8_t clearedAddress[IP_ADD_LENGTH];
    memset(clearedAddress, 0, IP_ADD_LENGTH);
    setIpAddress(clearedAddress);
    requestDhcpNewAddress();
}

static void setBound(void)
{
    stopTimers();
    stopTimer(callbackDhcpGetNewAddressTimer);
    setIpAddress(dhcpOfferedIpAdd);
    dhcpState = DHCP_BOUND;
    startOneshotTimer(callbackDhcpT1HitTimer, leaseT1);
    startOneshotTimer(callbackDhcpT2HitTimer, leaseT2);
    startOneshotTimer(callbackDhcpLeaseEndTimer, leaseSeconds);
}

void processDhcpResponse(etherHeader *ether)
{
    if (!isDhcpResponse(ether))
        return;

    switch (dhcpState)
    {
    case DHCP_SELECTING:
        if (isDhcpOffer(ether, dhcpOfferedIpAdd))
        {
            putsUart0("DHCP: got OFFER\n");
            getOptVal(ether, 54, dhcpServerIpAdd, IP_ADD_LENGTH);
            setDhcpState(DHCP_REQUESTING);
            requestNeeded = true;
        }
        break;

    case DHCP_REQUESTING:
        if (isDhcpAck(ether))
        {
            putsUart0("DHCP: got ACK\n");
            handleDhcpAck(ether);
            setDhcpState(DHCP_TESTING_IP);
            uint8_t probeSource[IP_ADD_LENGTH];
            memset(probeSource, 0, IP_ADD_LENGTH);
            sendArpRequest(ether, probeSource, dhcpOfferedIpAdd);
            requestDhcpIpConflictTest();
            stopTimer(callbackDhcpGetNewAddressTimer);
        }
        else if (isDhcpNak(ether))
        {
            putsUart0("DHCP: got NAK\n");
            stopTimer(callbackDhcpGetNewAddressTimer);
            handleNak();
        }
        break;

    case DHCP_RENEWING:
    case DHCP_REBINDING:
        if (isDhcpAck(ether))
        {
            putsUart0("DHCP: renew ACK\n");
            handleDhcpAck(ether);
            setBound();
        }
        else if (isDhcpNak(ether))
            handleNak();
        break;

    default:
        break;
    }
}

void processDhcpArpResponse(etherHeader *ether)
{
    arpPacket *arp = (arpPacket*)ether->data;

    if (dhcpState != DHCP_TESTING_IP)
        return;
    if (!ipConflictDetectionMode)
        return;

    uint8_t *senderIp = arp->sourceIp;
    bool sameIp = true;
    int i;
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        if (senderIp[i] != dhcpOfferedIpAdd[i])
        {
            sameIp = false;
            break;
        }
    }

    if (sameIp)
    {
        stopTimer(callbackDhcpIpConflictWindow);
        sendDhcpMessage(ether, DHCPDECLINE);
        setDhcpState(DHCP_INIT);
        ipConflictDetectionMode = false;
        startOneshotTimer(requestDhcpNewAddress, 15);
    }
}
// DHCP control functions

void enableDhcp()
{
    dhcpEnabled = true;
    putsUart0("DHCP enabled\n");
    requestDhcpNewAddress();
}

void disableDhcp()
{
    dhcpEnabled = false;
    stopTimers();
    stopTimer(callbackDhcpGetNewAddressTimer);
    setDhcpState(DHCP_DISABLED);
    leaseSeconds = 0;
    putsUart0("DHCP disabled\n");
}

bool isDhcpEnabled()
{
    return dhcpEnabled;
}

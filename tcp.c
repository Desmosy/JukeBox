// TCP Library
// Jason Losh

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
#include "arp.h"
#include "tcp.h"
#include "mqtt.h"
#include "timer.h"
#include "uart0.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

#define MAX_TCP_PORTS 4

uint16_t tcpPorts[MAX_TCP_PORTS];
uint8_t tcpPortCount = 0;
uint8_t tcpState[MAX_TCP_PORTS];

// Pending connection flags
bool tcpConnectPending = false;
bool tcpSynPending = false;
bool tcpArpWaiting = false;

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Set TCP state
void setTcpState(uint8_t instance, uint8_t state)
{
    tcpState[instance] = state;
}

// Get TCP state
uint8_t getTcpState(uint8_t instance)
{
    return tcpState[instance];
}

// Extract flags from the offsetFields word (lower 9 bits after byte swap)
uint16_t getTcpFlags(tcpHeader *tcp)
{
    return ntohs(tcp->offsetFields) & 0x01FF;
}

// Get TCP header length in bytes from the data offset field
uint16_t getTcpHeaderLength(tcpHeader *tcp)
{
    return ((ntohs(tcp->offsetFields) >> 12) & 0x0F) * 4;
}

// Get TCP payload data length
// Total TCP segment length (from IP) minus the TCP header size
uint16_t getTcpDataLength(ipHeader *ip, tcpHeader *tcp)
{
    uint16_t ipTotalLen = ntohs(ip->length);
    uint16_t ipHdrLen = ip->size * 4;
    uint16_t tcpHdrLen = getTcpHeaderLength(tcp);
    return ipTotalLen - ipHdrLen - tcpHdrLen;
}

// Determines whether packet is TCP packet
// Must be an IP packet. Validates protocol field and TCP checksum.
bool isTcp(etherHeader* ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    bool ok;
    uint32_t sum = 0;

    ok = (ip->protocol == PROTOCOL_TCP);
    if (ok)
    {
        // Verify TCP checksum using pseudo-header
        tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
        uint16_t tcpLength = ntohs(ip->length) - ipHeaderLength;

        // Sum pseudo-header fields: src IP + dst IP (8 bytes)
        sumIpWords(ip->sourceIp, 8, &sum);
        // Protocol (zero-padded to 16 bits)
        uint16_t tmp = htons(PROTOCOL_TCP);
        sumIpWords(&tmp, 2, &sum);
        // TCP segment length
        tmp = htons(tcpLength);
        sumIpWords(&tmp, 2, &sum);
        // Sum over entire TCP segment (header + data)
        sumIpWords(tcp, tcpLength, &sum);
        ok = (getIpChecksum(sum) == 0);
    }
    return ok;
}

// Check if SYN flag is set
bool isTcpSyn(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    return (getTcpFlags(tcp) & SYN) != 0;
}

// Check if ACK flag is set
bool isTcpAck(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    return (getTcpFlags(tcp) & ACK) != 0;
}

// Check if FIN flag is set
bool isTcpFin(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    return (getTcpFlags(tcp) & FIN) != 0;
}

// Check if RST flag is set
bool isTcpRst(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    return (getTcpFlags(tcp) & RST) != 0;
}

// Store open ports that the TM4C listens on
void setTcpPortList(uint16_t ports[], uint8_t count)
{
    uint8_t i;
    for (i = 0; i < count && i < MAX_TCP_PORTS; i++)
        tcpPorts[i] = ports[i];
    tcpPortCount = count;
}

// Check if a received TCP packet is destined for an open port
bool isTcpPortOpen(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);
    uint16_t destPort = ntohs(tcp->destPort);
    uint8_t i;
    for (i = 0; i < tcpPortCount; i++)
    {
        if (destPort == tcpPorts[i])
            return true;
    }
    // Also match if this packet is for our active MQTT connection
    socket *ms = getMqttSocket();
    if (ms->state != TCP_CLOSED && destPort == ms->localPort)
        return true;
    // Match port 80 for HTTP web server
    if (destPort == 80)
        return true;
    return false;
}

// Build and send a TCP segment.
// This constructs Ethernet + IP + TCP headers, appends optional data,
// calculates all checksums, and sends the packet via the ENC28J60.
void sendTcpMessage(etherHeader *ether, socket *s, uint16_t flags, uint8_t data[], uint16_t dataSize)
{
    uint8_t i;
    uint16_t j;  // Use 16-bit counter for data copy (payloads can exceed 255 bytes)
    uint8_t localHwAddress[HW_ADD_LENGTH];
    uint8_t localIpAddress[IP_ADD_LENGTH];

    // --- Ethernet header ---
    getEtherMacAddress(localHwAddress);
    for (i = 0; i < HW_ADD_LENGTH; i++)
    {
        ether->destAddress[i] = s->remoteHwAddress[i];
        ether->sourceAddress[i] = localHwAddress[i];
    }
    ether->frameType = htons(TYPE_IP);

    // --- IP header ---
    ipHeader *ip = (ipHeader*)ether->data;
    ip->rev = 0x4;                             // IPv4
    ip->size = 0x5;                            // 20-byte IP header, no options
    ip->typeOfService = 0;
    uint16_t tcpHeaderSize = 20;               // Default TCP header size
    // If sending SYN, add 4 bytes for the MSS option
    if (flags & SYN)
        tcpHeaderSize = 24;
    ip->length = htons(20 + tcpHeaderSize + dataSize);
    ip->id = 0;
    ip->flagsAndOffset = 0;
    ip->ttl = 128;
    ip->protocol = PROTOCOL_TCP;
    ip->headerChecksum = 0;
    getIpAddress(localIpAddress);
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        ip->sourceIp[i] = localIpAddress[i];
        ip->destIp[i] = s->remoteIpAddress[i];
    }

    // --- TCP header ---
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + (ip->size * 4));
    tcp->sourcePort = htons(s->localPort);
    tcp->destPort = htons(s->remotePort);
    tcp->sequenceNumber = htonl(s->sequenceNumber);
    tcp->acknowledgementNumber = htonl(s->acknowledgementNumber);

    // Data offset = header length in 32-bit words, placed in upper 4 bits
    uint16_t dataOffset;
    if (flags & SYN)
        dataOffset = 6;    // 24 bytes / 4 = 6 words
    else
        dataOffset = 5;    // 20 bytes / 4 = 5 words
    tcp->offsetFields = htons((dataOffset << OFS_SHIFT) | flags);

    tcp->windowSize = htons(1460);
    tcp->checksum = 0;
    tcp->urgentPointer = 0;

    // If SYN, add MSS option (kind=2, length=4, value=1460)
    uint8_t *tcpData;
    if (flags & SYN)
    {
        uint8_t *options = tcp->data;
        options[0] = 2;                        // MSS option kind
        options[1] = 4;                        // MSS option length
        options[2] = HIBYTE(1460);             // MSS value high byte
        options[3] = LOBYTE(1460);             // MSS value low byte
        tcpData = options + 4;
    }
    else
    {
        tcpData = tcp->data;
    }

    // Copy application data into the TCP payload area
    for (j = 0; j < dataSize; j++)
        tcpData[j] = data[j];

    // --- Calculate IP header checksum ---
    calcIpChecksum(ip);

    // --- Calculate TCP checksum over pseudo-header + TCP segment ---
    uint32_t sum = 0;
    uint16_t tmp;
    // Pseudo-header: source IP + dest IP (8 bytes)
    sumIpWords(ip->sourceIp, 8, &sum);
    // Pseudo-header: zero-padded protocol
    tmp = htons(PROTOCOL_TCP);
    sumIpWords(&tmp, 2, &sum);
    // Pseudo-header: TCP segment length
    tmp = htons(tcpHeaderSize + dataSize);
    sumIpWords(&tmp, 2, &sum);
    // TCP header + options + data
    sumIpWords(tcp, tcpHeaderSize + dataSize, &sum);
    tcp->checksum = getIpChecksum(sum);

    // --- Send the packet ---
    // Total frame = 14 (Ether) + 20 (IP) + tcpHeaderSize + dataSize
    putEtherPacket(ether, 14 + 20 + tcpHeaderSize + dataSize);

    // --- Update sequence number for next outgoing message ---
    // SYN and FIN each consume 1 sequence number
    if (flags & SYN)
        s->sequenceNumber += 1;
    else if (flags & FIN)
        s->sequenceNumber += 1;
    // Data bytes consume dataSize sequence numbers
    if (dataSize > 0)
        s->sequenceNumber += dataSize;
}

// Shortcut: send an empty TCP control segment (e.g. ACK, RST, FIN)
void sendTcpResponse(etherHeader *ether, socket* s, uint16_t flags)
{
    sendTcpMessage(ether, s, flags, NULL, 0);
}

// TCP state machine — processes incoming TCP packets for the MQTT connection
//
// Implements the client-side FSM from RFC 793:
//   CLOSED → SYN_SENT → ESTABLISHED → FIN_WAIT_1 → FIN_WAIT_2 → TIME_WAIT → CLOSED
//   ESTABLISHED → CLOSE_WAIT → LAST_ACK → CLOSED  (passive close)
void processTcpResponse(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);

    uint16_t flags = getTcpFlags(tcp);
    uint32_t remoteSeq = ntohl(tcp->sequenceNumber);
    uint32_t remoteAck = ntohl(tcp->acknowledgementNumber);
    uint16_t dataLen = getTcpDataLength(ip, tcp);

    socket *ms = getMqttSocket();

    // Only process packets that belong to our MQTT connection
    if (ntohs(tcp->destPort) != ms->localPort)
        return;

    switch (ms->state)
    {
        case TCP_SYN_SENT:
            // Expecting SYN+ACK from the MQTT broker
            if ((flags & SYN) && (flags & ACK))
            {
                // Populate socket with server's HW/IP info from this packet
                getSocketInfoFromTcpPacket(ether, ms);
                ms->acknowledgementNumber = remoteSeq + 1;   // ACK their SYN
                ms->sequenceNumber = remoteAck;               // Our next seq = their ACK

                // Send ACK to complete the 3-way handshake
                sendTcpResponse(ether, ms, ACK);

                ms->state = TCP_ESTABLISHED;
                putsUart0("TCP: Established\n");

                // TCP is up — now send the MQTT CONNECT packet
                sendMqttConnect(ether);
            }
            else if (flags & RST)
            {
                ms->state = TCP_CLOSED;
                putsUart0("TCP: Connection refused (RST)\n");
            }
            break;

        case TCP_ESTABLISHED:
            if (flags & RST)
            {
                ms->state = TCP_CLOSED;
                putsUart0("TCP: Reset by peer\n");
                break;
            }
            if (flags & FIN)
            {
                // Passive close — the broker wants to disconnect
                ms->acknowledgementNumber = remoteSeq + 1;
                sendTcpResponse(ether, ms, ACK);
                ms->state = TCP_CLOSE_WAIT;

                // Send our FIN to complete the shutdown
                sendTcpResponse(ether, ms, FIN | ACK);
                ms->state = TCP_LAST_ACK;
                putsUart0("TCP: Server closing connection\n");
            }
            else if (flags & ACK)
            {
                // Update our sequence number to what the server acknowledged
                ms->sequenceNumber = remoteAck;

                // If there is payload data, it is an MQTT packet
                if (dataLen > 0)
                {
                    ms->acknowledgementNumber = remoteSeq + dataLen;

                    // Pointer to TCP payload = start of MQTT data
                    uint8_t *tcpDataPtr = (uint8_t*)tcp + getTcpHeaderLength(tcp);

                    // Hand off to the MQTT layer for parsing
                    processMqttResponse(ether, tcpDataPtr, dataLen);

                    // ACK the received data
                    sendTcpResponse(ether, ms, ACK);
                }
            }
            break;

        case TCP_FIN_WAIT_1:
            if ((flags & FIN) && (flags & ACK))
            {
                // Server simultaneously ACKed our FIN and sent theirs
                ms->acknowledgementNumber = remoteSeq + 1;
                sendTcpResponse(ether, ms, ACK);
                ms->state = TCP_CLOSED;
                putsUart0("TCP: Closed\n");
            }
            else if (flags & ACK)
            {
                ms->state = TCP_FIN_WAIT_2;
            }
            break;

        case TCP_FIN_WAIT_2:
            if (flags & FIN)
            {
                ms->acknowledgementNumber = remoteSeq + 1;
                sendTcpResponse(ether, ms, ACK);
                ms->state = TCP_CLOSED;
                putsUart0("TCP: Closed\n");
            }
            break;

        case TCP_LAST_ACK:
            if (flags & ACK)
            {
                ms->state = TCP_CLOSED;
                putsUart0("TCP: Closed\n");
            }
            break;

        default:
            break;
    }
}

// Called when an ARP response is received.
// If the response is from the MQTT broker, we now have the MAC address
// needed to send the TCP SYN.
void processTcpArpResponse(etherHeader *ether)
{
    arpPacket *arp = (arpPacket*)ether->data;
    socket *ms = getMqttSocket();

    // Only care about ARP responses when we're waiting for the broker's MAC
    if (ms->state != TCP_CLOSED || !tcpArpWaiting)
        return;

    // Check if this ARP response came from the MQTT broker's IP
    uint8_t mqttIp[IP_ADD_LENGTH];
    getIpMqttBrokerAddress(mqttIp);
    bool match = true;
    uint8_t i;
    for (i = 0; i < IP_ADD_LENGTH; i++)
    {
        if (arp->sourceIp[i] != mqttIp[i])
            match = false;
    }
    if (match)
    {
        // Store the broker's MAC address into the socket
        getSocketInfoFromArpResponse(ether, ms);
        tcpArpWaiting = false;
        tcpSynPending = true;
        putsUart0("TCP: Broker MAC resolved\n");
    }
}

// Called every iteration of the main loop.
// Sends pending ARP requests or TCP SYN messages.
void sendTcpPendingMessages(etherHeader *ether)
{
    socket *ms = getMqttSocket();

    if (tcpConnectPending)
    {
        // We need the broker's MAC — send an ARP request
        uint8_t localIp[IP_ADD_LENGTH];
        getIpAddress(localIp);
        sendArpRequest(ether, localIp, ms->remoteIpAddress);
        tcpConnectPending = false;
        tcpArpWaiting = true;
        putsUart0("TCP: ARP request sent for broker\n");
    }
    if (tcpSynPending)
    {
        // We know the MAC, send the SYN to begin the TCP handshake
        sendTcpMessage(ether, ms, SYN, NULL, 0);
        ms->state = TCP_SYN_SENT;
        tcpSynPending = false;
        putsUart0("TCP: SYN sent\n");
    }
}

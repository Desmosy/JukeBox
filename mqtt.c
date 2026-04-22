// MQTT Library
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
#include "mqtt.h"
#include "tcp.h"
#include "arp.h"
#include "timer.h"
#include "uart0.h"
#include "music.h"

// ------------------------------------------------------------------------------
//  Globals
// ------------------------------------------------------------------------------

// The single TCP socket used for the MQTT broker connection
socket mqttSocket;

// Incrementing packet ID for SUBSCRIBE/UNSUBSCRIBE (must be non-zero)
uint16_t mqttPacketId = 1;

// Flag set true once CONNACK with return code 0 is received
bool mqttConnected = false;

// Store a pointer to the ether buffer so publish/subscribe can send
// without needing ether passed as a parameter
static etherHeader *mqttEtherPtr = NULL;

// MQTT credentials for authenticated brokers (e.g. Adafruit IO)
static char mqttUsername[MQTT_MAX_USER] = "";
static char mqttPassword[MQTT_MAX_PASS] = "";

// Flag to indicate MQTT CONNECT should be sent after TCP ESTABLISHED
// (not needed anymore since tcp.c calls sendMqttConnect directly,
//  but kept for reference)

// ------------------------------------------------------------------------------
//  Structures
// ------------------------------------------------------------------------------

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

// Return pointer to the MQTT socket (used by tcp.c)
socket* getMqttSocket()
{
    return &mqttSocket;
}

// Returns true if MQTT session is active
bool isMqttConnected()
{
    return mqttConnected && (mqttSocket.state == TCP_ESTABLISHED);
}

// --- CONNECT FLOW ---
// Called from the UART shell when user types "mqtt connect".
// This kicks off: ARP → TCP 3-way handshake → MQTT CONNECT → CONNACK
void connectMqtt()
{
    uint8_t i;
    uint8_t mqttIp[IP_ADD_LENGTH];
    getIpMqttBrokerAddress(mqttIp);

    // Verify broker IP is configured
    if (mqttIp[0] == 0 && mqttIp[1] == 0 && mqttIp[2] == 0 && mqttIp[3] == 0)
    {
        putsUart0("MQTT: Broker IP not set. Use 'set mqtt w.x.y.z'\n");
        return;
    }

    // Populate the MQTT socket
    for (i = 0; i < IP_ADD_LENGTH; i++)
        mqttSocket.remoteIpAddress[i] = mqttIp[i];
    mqttSocket.remotePort = 1883;                      // Standard MQTT port
    mqttSocket.localPort = 49152 + (random32() & 0xFF); // Ephemeral source port
    mqttSocket.sequenceNumber = random32();             // Random initial sequence number
    mqttSocket.acknowledgementNumber = 0;
    mqttSocket.state = TCP_CLOSED;
    mqttConnected = false;

    // Tell the TCP layer to begin connection (ARP first, then SYN)
    extern bool tcpConnectPending;
    tcpConnectPending = true;

    putsUart0("MQTT: Connecting to broker...\n");
}

// Set MQTT username and password for authenticated brokers
// Call this before "mqtt connect" if using Adafruit IO
void setMqttCredentials(const char *user, const char *pass)
{
    uint8_t i;
    for (i = 0; i < MQTT_MAX_USER - 1 && user[i] != '\0'; i++)
        mqttUsername[i] = user[i];
    mqttUsername[i] = '\0';

    for (i = 0; i < MQTT_MAX_PASS - 1 && pass[i] != '\0'; i++)
        mqttPassword[i] = pass[i];
    mqttPassword[i] = '\0';

    putsUart0("MQTT: Credentials set (user=");
    putsUart0(mqttUsername);
    putsUart0(")\n");
}

// --- MQTT CONNECT packet ---
// Called by tcp.c once the TCP 3-way handshake completes.
// Builds the CONNECT packet per MQTT v3.1.1 spec and sends it.
void sendMqttConnect(etherHeader *ether)
{
    // Save ether pointer for later use by publish/subscribe
    mqttEtherPtr = ether;

    char *clientId = "tm4c-kiosk";
    uint8_t clientIdLen = strlen(clientId);
    uint8_t usernameLen = strlen(mqttUsername);
    uint8_t passwordLen = strlen(mqttPassword);
    bool useAuth = (usernameLen > 0 && passwordLen > 0);

    // Variable header (10 bytes) + Payload (2 + clientIdLen)
    // If auth: + (2 + usernameLen) + (2 + passwordLen)
    uint16_t remainingLength = 10 + 2 + clientIdLen;
    if (useAuth)
        remainingLength += 2 + usernameLen + 2 + passwordLen;

    uint8_t mqttData[128];
    uint8_t idx = 0;
    uint8_t i;

    // Fixed header
    mqttData[idx++] = (MQTT_CONNECT << 4);             // Packet type = CONNECT
    mqttData[idx++] = (uint8_t)remainingLength;        // Remaining length (< 128)

    // Variable header: Protocol Name "MQTT"
    mqttData[idx++] = 0x00;                            // Length MSB
    mqttData[idx++] = 0x04;                            // Length LSB
    mqttData[idx++] = 'M';
    mqttData[idx++] = 'Q';
    mqttData[idx++] = 'T';
    mqttData[idx++] = 'T';

    // Protocol Level: 4 = MQTT v3.1.1
    mqttData[idx++] = 0x04;

    // Connect Flags:
    //   0x02 = Clean Session only
    //   0xC2 = Username + Password + Clean Session
    if (useAuth)
        mqttData[idx++] = 0xC2;
    else
        mqttData[idx++] = 0x02;

    // Keep Alive (seconds), MSB first
    mqttData[idx++] = HIBYTE(MQTT_KEEPALIVE_S);
    mqttData[idx++] = LOBYTE(MQTT_KEEPALIVE_S);

    // Payload: Client ID (2-byte length prefix + UTF-8 string)
    mqttData[idx++] = 0x00;
    mqttData[idx++] = clientIdLen;
    for (i = 0; i < clientIdLen; i++)
        mqttData[idx++] = clientId[i];

    // Payload: Username (only if auth is enabled)
    if (useAuth)
    {
        mqttData[idx++] = 0x00;
        mqttData[idx++] = usernameLen;
        for (i = 0; i < usernameLen; i++)
            mqttData[idx++] = mqttUsername[i];

        // Payload: Password (AIO key)
        mqttData[idx++] = 0x00;
        mqttData[idx++] = passwordLen;
        for (i = 0; i < passwordLen; i++)
            mqttData[idx++] = mqttPassword[i];
    }

    // Send via TCP with PSH+ACK
    sendTcpMessage(ether, &mqttSocket, PSH | ACK, mqttData, idx);
    putsUart0("MQTT: CONNECT sent");
    if (useAuth)
        putsUart0(" (with auth)");
    putsUart0("\n");
}

// --- DISCONNECT ---
// Sends MQTT DISCONNECT, then initiates TCP FIN
void disconnectMqtt()
{
    if (mqttSocket.state != TCP_ESTABLISHED)
    {
        putsUart0("MQTT: Not connected\n");
        return;
    }

    // Stop the keep-alive ping timer
    stopTimer(mqttPingCallback);

    // Send MQTT DISCONNECT (type 14, remaining length 0)
    uint8_t mqttData[2];
    mqttData[0] = (MQTT_DISCONNECT << 4);              // 0xE0
    mqttData[1] = 0x00;

    sendTcpMessage(mqttEtherPtr, &mqttSocket, PSH | ACK, mqttData, 2);

    // Close the TCP connection (active close)
    sendTcpResponse(mqttEtherPtr, &mqttSocket, FIN | ACK);
    mqttSocket.state = TCP_FIN_WAIT_1;
    mqttConnected = false;

    putsUart0("MQTT: Disconnecting...\n");
}

// --- PUBLISH (QoS 0) ---
// Publishes a message to the given topic
void publishMqtt(char strTopic[], char strData[])
{
    if (!isMqttConnected())
    {
        putsUart0("MQTT: Not connected\n");
        return;
    }

    uint8_t topicLen = strlen(strTopic);
    uint8_t dataLen = strlen(strData);

    // Remaining length = 2 (topic length field) + topicLen + dataLen
    // No packet ID for QoS 0
    uint16_t remainingLength = 2 + topicLen + dataLen;

    uint8_t mqttData[256];
    uint8_t idx = 0;
    uint8_t i;

    // Fixed header: PUBLISH, QoS 0, no retain, no DUP
    mqttData[idx++] = (MQTT_PUBLISH << 4);              // 0x30
    // Encode remaining length (supports up to 127 with single byte)
    mqttData[idx++] = (uint8_t)remainingLength;

    // Variable header: Topic Name (2-byte length + UTF-8)
    mqttData[idx++] = HIBYTE(topicLen);
    mqttData[idx++] = LOBYTE(topicLen);
    for (i = 0; i < topicLen; i++)
        mqttData[idx++] = strTopic[i];

    // Payload: application data
    for (i = 0; i < dataLen; i++)
        mqttData[idx++] = strData[i];

    sendTcpMessage(mqttEtherPtr, &mqttSocket, PSH | ACK, mqttData, idx);

    putsUart0("MQTT: Published ");
    putsUart0(strTopic);
    putsUart0(" = ");
    putsUart0(strData);
    putsUart0("\n");
}

// --- SUBSCRIBE (QoS 0) ---
// Subscribes to a topic filter
void subscribeMqtt(char strTopic[])
{
    if (!isMqttConnected())
    {
        putsUart0("MQTT: Not connected\n");
        return;
    }

    uint8_t topicLen = strlen(strTopic);

    // Remaining length = 2 (packet ID) + 2 (topic length) + topicLen + 1 (QoS)
    uint16_t remainingLength = 2 + 2 + topicLen + 1;

    uint8_t mqttData[256];
    uint8_t idx = 0;
    uint8_t i;

    // Fixed header: SUBSCRIBE type with required flags (bit 1 set)
    mqttData[idx++] = (MQTT_SUBSCRIBE << 4) | 0x02;    // 0x82
    mqttData[idx++] = (uint8_t)remainingLength;

    // Variable header: Packet Identifier (MSB first)
    mqttData[idx++] = HIBYTE(mqttPacketId);
    mqttData[idx++] = LOBYTE(mqttPacketId);
    mqttPacketId++;
    if (mqttPacketId == 0) mqttPacketId = 1;           // Packet ID must be non-zero

    // Payload: Topic Filter (2-byte length + UTF-8) + Requested QoS
    mqttData[idx++] = HIBYTE(topicLen);
    mqttData[idx++] = LOBYTE(topicLen);
    for (i = 0; i < topicLen; i++)
        mqttData[idx++] = strTopic[i];
    mqttData[idx++] = 0x00;                            // Requested QoS 0

    sendTcpMessage(mqttEtherPtr, &mqttSocket, PSH | ACK, mqttData, idx);

    putsUart0("MQTT: Subscribe ");
    putsUart0(strTopic);
    putsUart0("\n");
}

// --- UNSUBSCRIBE ---
// Unsubscribes from a topic filter
void unsubscribeMqtt(char strTopic[])
{
    if (!isMqttConnected())
    {
        putsUart0("MQTT: Not connected\n");
        return;
    }

    uint8_t topicLen = strlen(strTopic);

    // Remaining length = 2 (packet ID) + 2 (topic length) + topicLen
    uint16_t remainingLength = 2 + 2 + topicLen;

    uint8_t mqttData[256];
    uint8_t idx = 0;
    uint8_t i;

    // Fixed header: UNSUBSCRIBE type with required flags (bit 1 set)
    mqttData[idx++] = (MQTT_UNSUBSCRIBE << 4) | 0x02;  // 0xA2
    mqttData[idx++] = (uint8_t)remainingLength;

    // Packet Identifier
    mqttData[idx++] = HIBYTE(mqttPacketId);
    mqttData[idx++] = LOBYTE(mqttPacketId);
    mqttPacketId++;
    if (mqttPacketId == 0) mqttPacketId = 1;

    // Topic Filter
    mqttData[idx++] = HIBYTE(topicLen);
    mqttData[idx++] = LOBYTE(topicLen);
    for (i = 0; i < topicLen; i++)
        mqttData[idx++] = strTopic[i];

    sendTcpMessage(mqttEtherPtr, &mqttSocket, PSH | ACK, mqttData, idx);

    putsUart0("MQTT: Unsubscribe ");
    putsUart0(strTopic);
    putsUart0("\n");
}

// --- PINGREQ ---
// Sends a keep-alive ping to prevent broker timeout.
// Called by the periodic timer every MQTT_KEEPALIVE_S/2 seconds.
void sendMqttPingReq(etherHeader *ether)
{
    if (mqttSocket.state != TCP_ESTABLISHED)
        return;

    uint8_t mqttData[2];
    mqttData[0] = (MQTT_PINGREQ << 4);                // 0xC0
    mqttData[1] = 0x00;

    sendTcpMessage(ether, &mqttSocket, PSH | ACK, mqttData, 2);
}

// Timer callback for periodic keep-alive pings
void mqttPingCallback()
{
    if (mqttEtherPtr != NULL && mqttSocket.state == TCP_ESTABLISHED)
    {
        sendMqttPingReq(mqttEtherPtr);
    }
}

// --- PROCESS INCOMING MQTT PACKETS ---
// Called by tcp.c when TCP data arrives on the MQTT socket.
// Parses the MQTT fixed header to determine packet type, then handles it.
void processMqttResponse(etherHeader *ether, uint8_t *data, uint16_t length)
{
    if (length < 2) return;

    // Save ether pointer
    mqttEtherPtr = ether;

    uint8_t packetType = (data[0] >> 4) & 0x0F;

    switch (packetType)
    {
        case MQTT_CONNACK:
        {
            // CONNACK: byte 0 = type, byte 1 = remaining len (2),
            //          byte 2 = session present flag, byte 3 = return code
            if (length >= 4)
            {
                uint8_t returnCode = data[3];
                if (returnCode == 0)
                {
                    mqttConnected = true;
                    putsUart0("MQTT: Connected!\n");

                    // Start the keep-alive ping timer
                    // Send pings at half the keep-alive interval for safety
                    startPeriodicTimer(mqttPingCallback, MQTT_KEEPALIVE_S / 2);

                    // Auto-subscribe to Team 18's required topics
                    subscribeMqtt("music_set_name_1");
                    subscribeMqtt("music_set_name_2");
                    subscribeMqtt("music_set_name_3");
                    subscribeMqtt("music_show_playing");
                }
                else
                {
                    char str[40];
                    snprintf(str, sizeof(str), "MQTT: Connect refused (code %d)\n", returnCode);
                    putsUart0(str);
                    mqttConnected = false;
                }
            }
            break;
        }

        case MQTT_PUBLISH:
        {
            // A message was published to a topic we subscribed to.
            // Parse the topic and payload, then display on UART.
            uint8_t idx = 1;

            // Decode remaining length (variable length encoding)
            uint32_t remainingLength = 0;
            uint8_t shift = 0;
            uint8_t encodedByte;
            do
            {
                if (idx >= length) return;
                encodedByte = data[idx++];
                remainingLength |= (uint32_t)(encodedByte & 0x7F) << shift;
                shift += 7;
            } while ((encodedByte & 0x80) != 0);

            // Topic length (2 bytes, MSB first)
            if (idx + 2 > length) return;
            uint16_t topicLen = (data[idx] << 8) | data[idx + 1];
            idx += 2;

            // Topic string
            char topic[64];
            uint8_t j;
            for (j = 0; j < topicLen && j < 63 && idx < length; j++)
                topic[j] = data[idx++];
            topic[j] = '\0';

            // Check QoS from fixed header flags (bits 2:1 of byte 0)
            uint8_t qos = (data[0] >> 1) & 0x03;
            uint16_t packetId = 0;
            if (qos > 0)
            {
                // Packet ID present for QoS 1 and 2
                if (idx + 2 <= length)
                {
                    packetId = (data[idx] << 8) | data[idx + 1];
                    idx += 2;
                }
            }

            // Payload = remaining bytes
            char payload[128];
            uint16_t payloadLen = length - idx;
            for (j = 0; j < payloadLen && j < 127; j++)
                payload[j] = data[idx++];
            payload[j] = '\0';

            // Display on UART
            putsUart0("  >> ");
            putsUart0(topic);
            putsUart0(" : ");
            putsUart0(payload);
            putsUart0("\n");

            // --- Route to Music module (Team 18 topics) ---
            if (strcmp(topic, "music_set_name_1") == 0)
                setSongName(0, payload);
            else if (strcmp(topic, "music_set_name_2") == 0)
                setSongName(1, payload);
            else if (strcmp(topic, "music_set_name_3") == 0)
                setSongName(2, payload);
            else if (strcmp(topic, "music_show_playing") == 0)
            {
                putsUart0("Music: Now playing -> ");
                putsUart0(payload);
                putsUart0("\n");
            }

            // If QoS 1, send PUBACK
            if (qos == 1 && packetId > 0)
            {
                uint8_t puback[4];
                puback[0] = (MQTT_PUBACK << 4);        // 0x40
                puback[1] = 0x02;                      // Remaining length = 2
                puback[2] = HIBYTE(packetId);
                puback[3] = LOBYTE(packetId);
                sendTcpMessage(ether, &mqttSocket, PSH | ACK, puback, 4);
            }
            break;
        }

        case MQTT_PUBACK:
            putsUart0("MQTT: Publish ACK\n");
            break;

        case MQTT_SUBACK:
        {
            // SUBACK: check if subscription was accepted
            if (length >= 5)
            {
                uint8_t resultCode = data[4];
                if (resultCode <= 2)
                    putsUart0("MQTT: Subscribed OK\n");
                else
                    putsUart0("MQTT: Subscribe FAILED\n");
            }
            else
            {
                putsUart0("MQTT: SUBACK received\n");
            }
            break;
        }

        case MQTT_UNSUBACK:
            putsUart0("MQTT: Unsubscribed OK\n");
            break;

        case MQTT_PINGRESP:
            // Keep-alive confirmed — connection is healthy
            break;

        default:
        {
            char str[40];
            snprintf(str, sizeof(str), "MQTT: Unknown type %d\n", packetType);
            putsUart0(str);
            break;
        }
    }
}

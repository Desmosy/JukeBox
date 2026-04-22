// HTTP Web Server Library
// Team 18 - Web Server Team 2
//
// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz
//
// Serves a voting kiosk web page on port 80.

#include <stdio.h>
#include <string.h>
#include "http.h"
#include "tcp.h"
#include "ip.h"
#include "mqtt.h"
#include "music.h"
#include "uart0.h"

static socket httpSocket;
static char httpBuf[1400];
static uint16_t httpLen;

static void hput(const char *s)
{
    while (*s && httpLen < sizeof(httpBuf) - 1)
        httpBuf[httpLen++] = *s++;
}

static void hputNum(uint16_t n)
{
    char tmp[6];
    uint8_t i = 0;
    if (n == 0)
    {
        if (httpLen < sizeof(httpBuf) - 1)
            httpBuf[httpLen++] = '0';
        return;
    }
    while (n > 0 && i < 5)
    {
        tmp[i++] = '0' + (n % 10);
        n /= 10;
    }
    while (i > 0)
    {
        i--;
        if (httpLen < sizeof(httpBuf) - 1)
            httpBuf[httpLen++] = tmp[i];
    }
}

void initHttp()
{
    httpSocket.state = TCP_CLOSED;
    httpSocket.localPort = HTTP_PORT;
    putsUart0("HTTP: Web server ready on port 80\n");
}

socket* getHttpSocket()
{
    return &httpSocket;
}

static void buildPage()
{
    uint8_t i;
    httpLen = 0;

    hput("<html><head><title>Team 18</title>");
    hput("<meta http-equiv=refresh content=10>");
    hput("<meta http-equiv=Cache-Control content=no-cache>");
    hput("<style>");
    hput("body{font-family:Arial;text-align:center;");
    hput("background:#1a1a2e;color:#fff;padding:20px}");
    hput("h1{color:#e94560}");
    hput(".c{background:#16213e;border-radius:10px;");
    hput("padding:12px;margin:8px auto;max-width:280px}");
    hput(".v{font-size:22px;color:#e94560}");
    hput("a{display:inline-block;background:#e94560;");
    hput("color:#fff;padding:8px 24px;border-radius:6px;");
    hput("text-decoration:none;margin:4px;font-weight:bold}");
    hput(".w{color:#2cb67d;font-size:20px;margin:10px}");
    hput(".lk{color:#e94560;font-size:14px}");
    hput(".f{color:#555;font-size:11px;margin-top:15px}");
    hput("</style></head><body>");
    hput("<h1>Music Vote</h1>");

    if (isVotingLocked())
    {
        // Show winner and locked state
        hput("<p class=w>Voting Closed!</p>");
        hput("<div class=c><b>Winner: ");
        uint8_t win = getWinner();
        if (win >= 1 && win <= NUM_SONGS)
            hput(getSongName(win - 1));
        hput("</b><br><span class=v>");
        if (win >= 1 && win <= NUM_SONGS)
            hputNum(getVoteCount(win - 1));
        hput(" votes</span></div>");

        // Show all results
        for (i = 0; i < NUM_SONGS; i++)
        {
            hput("<div class=c><b>");
            hput(getSongName(i));
            hput("</b> - ");
            hputNum(getVoteCount(i));
            hput(" votes</div>");
        }
        hput("<p class=lk>Now Playing... Reset from terminal</p>");
    }
    else
    {
        // Show vote buttons
        if (isVotingActive())
            hput("<p class=w>Vote now!</p>");

        for (i = 0; i < NUM_SONGS; i++)
        {
            hput("<div class=c><b>");
            hput(getSongName(i));
            hput("</b><br><span class=v>");
            hputNum(getVoteCount(i));
            hput(" votes</span><br>");
            hput("<a href=/vote?s=");
            hputNum(i + 1);
            hput(">Vote</a></div>");
        }
    }

    hput("<p class=f>Refreshes every 5s | Team 18</p>");
    hput("</body></html>");
}

static void processHttpRequest(etherHeader *ether, uint8_t *data, uint16_t dataLen)
{
    char req[80];
    uint16_t i;
    for (i = 0; i < dataLen && i < 79; i++)
    {
        if (data[i] == '\r' || data[i] == '\n') break;
        req[i] = data[i];
    }
    req[i] = '\0';

    putsUart0("HTTP: ");
    putsUart0(req);
    putsUart0("\n");

    // Vote handler: cast vote then redirect to /
    // The redirect prevents auto-refresh from re-voting
    if (strncmp(req, "GET /vote?s=", 12) == 0)
    {
        uint8_t songNum = req[12] - '0';
        if (songNum >= 1 && songNum <= 3)
            castVote(songNum);

        httpLen = 0;
        hput("HTTP/1.1 302 Found\r\n");
        hput("Location: /\r\n");
        hput("Content-Length: 0\r\n");
        hput("Connection: close\r\n");
        hput("\r\n");
        sendTcpMessage(ether, &httpSocket, PSH | ACK | FIN,
                       (uint8_t*)httpBuf, httpLen);
        httpSocket.state = TCP_FIN_WAIT_1;
        putsUart0("HTTP: Vote redirect\n");
        return;
    }

    // Favicon: 404
    if (strncmp(req, "GET /fav", 8) == 0)
    {
        httpLen = 0;
        hput("HTTP/1.1 404 Not Found\r\n");
        hput("Content-Length: 0\r\n");
        hput("Connection: close\r\n");
        hput("\r\n");
        sendTcpMessage(ether, &httpSocket, PSH | ACK | FIN,
                       (uint8_t*)httpBuf, httpLen);
        httpSocket.state = TCP_FIN_WAIT_1;
        return;
    }

    // Build HTML body
    buildPage();
    uint16_t bodyLen = httpLen;

    // Build HTTP headers
    char hdr[100];
    uint16_t hdrLen = 0;
    const char *h1 = "HTTP/1.1 200 OK\r\nContent-Type: text/html\r\nConnection: close\r\nContent-Length: ";
    uint16_t h1Len = strlen(h1);
    memcpy(hdr, h1, h1Len);
    hdrLen = h1Len;

    // Content-Length digits
    {
        char numBuf[6];
        char rev[6];
        uint8_t ni = 0;
        uint8_t ri = 0;
        uint16_t tmp = bodyLen;
        if (tmp == 0)
        {
            numBuf[ni++] = '0';
        }
        else
        {
            while (tmp > 0)
            {
                rev[ri++] = '0' + (tmp % 10);
                tmp /= 10;
            }
            while (ri > 0)
                numBuf[ni++] = rev[--ri];
        }
        memcpy(hdr + hdrLen, numBuf, ni);
        hdrLen += ni;
    }

    memcpy(hdr + hdrLen, "\r\n\r\n", 4);
    hdrLen += 4;

    // Combine headers + body
    if (hdrLen + bodyLen < sizeof(httpBuf))
    {
        memmove(httpBuf + hdrLen, httpBuf, bodyLen);
        memcpy(httpBuf, hdr, hdrLen);
        uint16_t totalLen = hdrLen + bodyLen;

        char dbg[40];
        snprintf(dbg, sizeof(dbg), "HTTP: Sending %d bytes\n", totalLen);
        putsUart0(dbg);

        sendTcpMessage(ether, &httpSocket, PSH | ACK | FIN,
                       (uint8_t*)httpBuf, totalLen);
    }
    else
    {
        putsUart0("HTTP: Response too large!\n");
        sendTcpMessage(ether, &httpSocket, PSH | ACK | FIN,
                       (uint8_t*)hdr, hdrLen);
    }

    httpSocket.state = TCP_FIN_WAIT_1;
    putsUart0("HTTP: Response sent\n");
}

void processHttpTcpPacket(etherHeader *ether)
{
    ipHeader *ip = (ipHeader*)ether->data;
    uint8_t ipHeaderLength = ip->size * 4;
    tcpHeader *tcp = (tcpHeader*)((uint8_t*)ip + ipHeaderLength);

    uint16_t flags = getTcpFlags(tcp);
    uint32_t remoteSeq = ntohl(tcp->sequenceNumber);
    uint32_t remoteAck = ntohl(tcp->acknowledgementNumber);
    uint16_t dataLen = getTcpDataLength(ip, tcp);

    // If a new SYN arrives while the socket is stuck in a closing state,
    // force-reset so we can accept the new connection immediately.
    // This prevents "connection timed out" when browsers send concurrent requests.
    if ((flags & SYN) && httpSocket.state != TCP_CLOSED &&
        httpSocket.state != TCP_LISTEN && httpSocket.state != TCP_SYN_RECEIVED)
    {
        httpSocket.state = TCP_CLOSED;
    }

    switch (httpSocket.state)
    {
        case TCP_CLOSED:
        case TCP_LISTEN:
            if (flags & SYN)
            {
                getSocketInfoFromTcpPacket(ether, &httpSocket);
                httpSocket.localPort = HTTP_PORT;
                httpSocket.acknowledgementNumber = remoteSeq + 1;
                httpSocket.sequenceNumber = random32();
                sendTcpResponse(ether, &httpSocket, SYN | ACK);
                httpSocket.state = TCP_SYN_RECEIVED;
            }
            break;

        case TCP_SYN_RECEIVED:
            if (flags & RST)
            {
                httpSocket.state = TCP_CLOSED;
                break;
            }
            if (flags & ACK)
            {
                httpSocket.sequenceNumber = remoteAck;
                httpSocket.state = TCP_ESTABLISHED;
                if (dataLen > 0)
                {
                    httpSocket.acknowledgementNumber = remoteSeq + dataLen;
                    uint8_t *tcpDataPtr = (uint8_t*)tcp + getTcpHeaderLength(tcp);
                    processHttpRequest(ether, tcpDataPtr, dataLen);
                }
            }
            break;

        case TCP_ESTABLISHED:
            if (flags & RST)
            {
                httpSocket.state = TCP_CLOSED;
                break;
            }
            if (flags & ACK)
            {
                httpSocket.sequenceNumber = remoteAck;
                if (dataLen > 0)
                {
                    httpSocket.acknowledgementNumber = remoteSeq + dataLen;
                    uint8_t *tcpDataPtr = (uint8_t*)tcp + getTcpHeaderLength(tcp);
                    processHttpRequest(ether, tcpDataPtr, dataLen);
                }
            }
            if (flags & FIN)
            {
                httpSocket.acknowledgementNumber = remoteSeq + 1;
                sendTcpResponse(ether, &httpSocket, ACK);
                httpSocket.state = TCP_CLOSED;
            }
            break;

        case TCP_FIN_WAIT_1:
            if ((flags & FIN) && (flags & ACK))
            {
                httpSocket.acknowledgementNumber = remoteSeq + 1;
                sendTcpResponse(ether, &httpSocket, ACK);
                httpSocket.state = TCP_CLOSED;
            }
            else if (flags & ACK)
            {
                httpSocket.state = TCP_FIN_WAIT_2;
            }
            else if (flags & FIN)
            {
                httpSocket.acknowledgementNumber = remoteSeq + 1;
                sendTcpResponse(ether, &httpSocket, ACK);
                httpSocket.state = TCP_CLOSED;
            }
            break;

        case TCP_FIN_WAIT_2:
            if (flags & FIN)
            {
                httpSocket.acknowledgementNumber = remoteSeq + 1;
                sendTcpResponse(ether, &httpSocket, ACK);
                httpSocket.state = TCP_CLOSED;
            }
            break;

        default:
            httpSocket.state = TCP_CLOSED;
            break;
    }
}

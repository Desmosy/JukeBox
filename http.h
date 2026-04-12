// HTTP Web Server Library
// Team 18 - Web Server Team 2

//-----------------------------------------------------------------------------
// Hardware Target
//-----------------------------------------------------------------------------

// Target Platform: EK-TM4C123GXL w/ ENC28J60
// Target uC:       TM4C123GH6PM
// System Clock:    40 MHz

// Serves a voting kiosk web page on port 80.
// Browsers connect, see vote buttons, and click to vote.

//-----------------------------------------------------------------------------
// Device includes, defines, and assembler directives
//-----------------------------------------------------------------------------

#ifndef HTTP_H_
#define HTTP_H_

#include <stdint.h>
#include <stdbool.h>
#include "ip.h"
#include "socket.h"

#define HTTP_PORT 80

//-----------------------------------------------------------------------------
// Subroutines
//-----------------------------------------------------------------------------

void initHttp();
socket* getHttpSocket();
void processHttpTcpPacket(etherHeader *ether);

#endif

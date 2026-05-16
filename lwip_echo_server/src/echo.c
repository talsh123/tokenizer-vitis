/*
 * Copyright (C) 2009 - 2019 Xilinx, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 3. The name of the author may not be used to endorse or promote products
 *    derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT
 * SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT
 * OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY
 * OF SUCH DAMAGE.
 *
 */

#include <stdio.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/tcp.h"
#if defined (__arm__) || defined (__aarch64__)
#include "xil_printf.h"
#endif
#include "xil_io.h"

/* ============================================================================
 * Tokenizer Hardware Interface
 *
 * Register Map (from tokenizer_axi_lite.v):
 *   0x00  TX_DATA  (W)  - Write an 8-bit ASCII byte into the input FIFO
 *   0x04  RX_DATA  (R)  - Read a 16-bit Token ID from the output FIFO
 *   0x08  STATUS   (R)  - Bit 0: input FIFO not full (1 = safe to write)
 *                         Bit 1: output FIFO not empty (1 = token available)
 *
 * IMPORTANT: Replace TOK_BASE_ADDR with the actual address assigned in
 *            Vivado's Address Editor for tokenizer_axi_lite_0/s_axi.
 * ============================================================================ */
#define TOK_BASE_ADDR  0x44A00000  /* <-- UPDATE THIS to match Address Editor */

#define TOK_TX_DATA    (TOK_BASE_ADDR + 0x00)
#define TOK_RX_DATA    (TOK_BASE_ADDR + 0x04)
#define TOK_STATUS     (TOK_BASE_ADDR + 0x08)

/* Check if input FIFO has space (bit 0 of STATUS) */
static inline int tok_can_write(void) {
    return Xil_In32(TOK_STATUS) & 0x1;
}

/* Check if output FIFO has a token (bit 1 of STATUS) */
static inline int tok_has_token(void) {
    return Xil_In32(TOK_STATUS) & 0x2;
}

/* Send one ASCII byte to the tokenizer */
static void tok_send_byte(u8 byte) {
    while (!tok_can_write());  /* wait for space in input FIFO */
    Xil_Out32(TOK_TX_DATA, (u32)byte);
}

/* Read one token ID from the tokenizer */
static u16 tok_read_token(void) {
    while (!tok_has_token());  /* wait for token in output FIFO */
    return (u16)Xil_In32(TOK_RX_DATA);
}

int transfer_data() {
    return 0;
}

void print_app_header()
{
#if (LWIP_IPV6==0)
    xil_printf("\n\r\n\r-----FPGA WordPiece Tokenizer Server------\n\r");
#else
    xil_printf("\n\r\n\r-----FPGA WordPiece Tokenizer Server (IPv6)------\n\r");
#endif
    xil_printf("Send text to port 7. Tokenized IDs will be returned.\n\r");
    xil_printf("Tokenizer HW base address: 0x%08X\n\r", TOK_BASE_ADDR);
}

/// ------------------------ MY CODE ------------------------
err_t recv_callback(void *arg, struct tcp_pcb *tpcb,
                               struct pbuf *p, err_t err)
{
    int i;
    u8 *payload;
    char resp_buf[2048];
    int resp_len = 0;
    int token_count = 0;
    u16 tid;

    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    tcp_recved(tpcb, p->len);

    payload = (u8 *)p->payload;

    xil_printf("Received %d bytes: ", p->len);
    for (i = 0; i < p->len && i < 80; i++) {
        if (payload[i] >= 0x20 && payload[i] < 0x7F)
            xil_printf("%c", payload[i]);
        else
            xil_printf(".");
    }
    xil_printf("\n\r");

    /* Send each byte to the tokenizer hardware, skip \r and \n */
    for (i = 0; i < p->len; i++) {
        u8 c = payload[i];
        if (c == '\r' || c == '\n')
            continue;
        tok_send_byte(c);
    }

    /* Send a trailing space to flush the last word */
    tok_send_byte(' ');

    /* Let the hardware pipeline finish processing */
    for (volatile int d = 0; d < 50000; d++);

    /* Read all available token IDs from the output FIFO */
    resp_len = 0;
    token_count = 0;

    while (tok_has_token()) {
        tid = tok_read_token();
        token_count++;

        resp_len += snprintf(resp_buf + resp_len, sizeof(resp_buf) - resp_len,
                             "%d ", tid);

        xil_printf("  Token[%d] = %d\n\r", token_count - 1, tid);

        if (resp_len >= (int)sizeof(resp_buf) - 20)
            break;
    }

    if (resp_len > 0) {
        resp_len += snprintf(resp_buf + resp_len, sizeof(resp_buf) - resp_len, "\r\n");
    } else {
        resp_len = snprintf(resp_buf, sizeof(resp_buf), "(no tokens)\r\n");
    }

    xil_printf("Total tokens: %d\n\r", token_count);

    if (tcp_sndbuf(tpcb) > resp_len) {
        err = tcp_write(tpcb, resp_buf, resp_len, 1);
    } else {
        xil_printf("no space in tcp_sndbuf\n\r");
    }

    pbuf_free(p);

    return ERR_OK;
}
/// ------------------------ END OF MY CODE ------------------------

err_t accept_callback(void *arg, struct tcp_pcb *newpcb, err_t err)
{
    static int connection = 1;

    /* set the receive callback for this connection */
    tcp_recv(newpcb, recv_callback);

    /* just use an integer number indicating the connection id as the
       callback argument */
    tcp_arg(newpcb, (void*)(UINTPTR)connection);

    /* increment for subsequent accepted connections */
    connection++;

    return ERR_OK;
}

int start_application()
{
    struct tcp_pcb *pcb;
    err_t err;
    unsigned port = 7;

    /* create new TCP PCB structure */
    pcb = tcp_new_ip_type(IPADDR_TYPE_ANY);
    if (!pcb) {
        xil_printf("Error creating PCB. Out of Memory\n\r");
        return -1;
    }

    /* bind to specified @port */
    err = tcp_bind(pcb, IP_ANY_TYPE, port);
    if (err != ERR_OK) {
        xil_printf("Unable to bind to port %d: err = %d\n\r", port, err);
        return -2;
    }

    /* we do not need any arguments to callback functions */
    tcp_arg(pcb, NULL);

    /* listen for connections */
    pcb = tcp_listen(pcb);
    if (!pcb) {
        xil_printf("Out of memory while tcp_listen\n\r");
        return -3;
    }

    /* specify callback to use for incoming connections */
    tcp_accept(pcb, accept_callback);

    xil_printf("TCP tokenizer server started @ port %d\n\r", port);

    return 0;
}
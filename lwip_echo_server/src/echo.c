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

/*
this is the main application file.
the stock Vitis lwIP Echo Server template gives us a simple TCP echo server that receives data and sends it back unchanged.
this echo.c file is modified to intercept incoming text, feed it through the tokenizer hardware and return token IDs. 
*/

#include <stdio.h>
#include <string.h>

#include "lwip/err.h"
#include "lwip/tcp.h"
#if defined (__arm__) || defined (__aarch64__)
#include "xil_printf.h"
#endif
#include "xil_io.h"

/// ------------------------ MY CODE ------------------------
// this brings the axi timer driver so you can measure tokenizer latency.
#include "xtmrctr.h"
// this timer variable is a global instance of the timer hardware driver.
XTmrCtr timer;

#define TOK_BASE_ADDR  0x44A00000 // TOK_BASE_ADDR maps to the register map in tokenizer_axi_lite.v - points to /tokenizer_axi_lite_0/s_axi address.
// these offsets match the case(s_axi_araddr[3:2]) login in Verilog
#define TOK_TX_DATA    (TOK_BASE_ADDR + 0x00) // this match 2'b00 = 0x00
#define TOK_RX_DATA    (TOK_BASE_ADDR + 0x04) // this match 2'b01 = 0x04
#define TOK_STATUS     (TOK_BASE_ADDR + 0x08) // this match 2'b10 = 0x08
/// ------------------------ END OF MY CODE ------------------------

// this function reads the status register and checks bit 0.
// basically takes the status register and ANDs it with 0x01.
// it returns non-zero if the input FIFO has space.
// Xil_In32 is a Xilinx helper that does a 32-bit-memory-mapped read - it compiles to a single lwi (load word immediate) instruction on MicroBlaze.
static inline int tok_can_write(void) {
    return Xil_In32(TOK_STATUS) & 0x1;
}

// this function reads the status register and checks bit 1.
// basically takes the status register and ANDs it with 0x02.
// it returns non-zero if the token is available in the output FIFO.
// Xil_In32 is a Xilinx helper that does a 32-bit-memory-mapped read - it compiles to a single lwi (load word immediate) instruction on MicroBlaze.
static inline int tok_has_token(void) {
    return Xil_In32(TOK_STATUS) & 0x2;
}

// this function polls STATUS until there's space, then writes one byte to TX_DATA
// Xil_Out32 is a Xilinx helper that does a 32-bit-memory-mapped write - the upper 24 bits of the write are zeros - the verilog looks at s_axi_wdata[7:0]
static void tok_send_byte(u8 byte) {
    while (!tok_can_write());  /* wait for space in input FIFO */
    Xil_Out32(TOK_TX_DATA, (u32)byte);
}

// this function polls STATUS until a token is available, then reads RX_DATA.
// the Verilog returns 16-bit token ID zero-extended to 32 bit, so casting to u16 is safe.
// Xil_In32 is a Xilinx helper that does a 32-bit-memory-mapped read - it compiles to a single lwi (load word immediate) instruction on MicroBlaze.
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
// we removed the template recv_callback and replaced it.

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

    // tcp_recved tell lwIP we've consumed p->len bytes from the TCP receive window.
    // this is critical - without it, lwIP would eventually stop accepting data because it thinks its receive buffer is full.
    // p->payload points to the raw TCP data bytes.
    tcp_recved(tpcb, p->len);
    payload = (u8 *)p->payload;

    // this counts the actual text characters, excluding telnet line endings "\r\n".
    // this is for the debug output only.
    int text_bytes = 0;
    for (i = 0; i < p->len; i++) {
        if (payload[i] != '\r' && payload[i] != '\n')
            text_bytes++;
    }

    // this captures the timer value before sending.
    // the timer counts upward from 0 continuously.
    u32 t_start = XTmrCtr_GetValue(&timer, 0);

    // sends each text character to the tokenizer hardware, skipping carriage return and newline
    // the trailing space is critical - it acts as a word boundary trigger.
    // without it, the last word would never get a word_done signal and its tokens would stay stuck in the pipeline
    for (i = 0; i < p->len; i++) {
        u8 c = payload[i];
        if (c == '\r' || c == '\n')
            continue;
        tok_send_byte(c);
    }
    tok_send_byte(' ');

    // captures the timing after sending.
    // t_send - t_start gives the send phase duration in timer ticks
    u32 t_sent = XTmrCtr_GetValue(&timer, 0);

    // this is a busy-wait loop.
    // this gives the tokenizer hardware time to finish processing the last word's backtracking and emit all tokens.
    // the volatile keyword prevents the compiler from optimizing the loop away.
    // 50,000 iterations at ~100MHz is roughly 500µs of delay.
    // this is a known limitation - in a production design, we need to use an interrupt or a "pipeline done" status bit instead of a blind delay.
    for (volatile int d = 0; d < 50000; d++);

    // this drains all tokens from the output FIFO, formatting each as a decimal number followed by a space.
    // the snprintf builds the response string incrementally.
    // the buffer overflow check >= sizeof - 20 prevents writing past the end of resp_buf
    while (tok_has_token()) {
        tid = tok_read_token();
        token_count++;
        resp_len += snprintf(resp_buf + resp_len, sizeof(resp_buf) - resp_len,
                             "%d ", tid);
        if (resp_len >= (int)sizeof(resp_buf) - 20)
            break;
    }

    // captures the timing after getting the value back.
    u32 t_end = XTmrCtr_GetValue(&timer, 0);

    // calculates the timing in µs.
    // division by 100 converts from 100MHz timer ticks (10ns each) to µs (1µs = 100 ticks).
    // these prints to UART for debugging.
    xil_printf("Received %d bytes (%d text)\n\r", p->len, text_bytes);
    xil_printf("Total tokens: %d\n\r", token_count);
    u32 send_us = (t_sent - t_start) / 100;
    u32 total_us = (t_end - t_start) / 100;
    xil_printf("Send: %u us | Total: %u us | Tokens: %d\n\r",
            send_us, total_us, token_count);

    /* formats the response print */
    if (resp_len > 0) {
        resp_len += snprintf(resp_buf + resp_len, sizeof(resp_buf) - resp_len, "\r\n");
    } else {
        resp_len = snprintf(resp_buf, sizeof(resp_buf), "(no tokens)\r\n");
    }

    // sends the tcp response
    if (tcp_sndbuf(tpcb) > resp_len) {
        err = tcp_write(tpcb, resp_buf, resp_len, 1);
    } else {
        xil_printf("no space in tcp_sndbuf\n\r");
    }

    // frees the lwIP packet buffer.
    // without this, you'd leak memory and eventually run out of pbufs.
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

    /// ------------------------ MY CODE ------------------------
    XTmrCtr_Initialize(&timer, XPAR_AXI_TIMER_0_BASEADDR); // initializes timer counter 0
    XTmrCtr_SetOptions(&timer, 0, XTC_AUTO_RELOAD_OPTION); // sets the timer with auto-reload mode (counts continuously without stopping an overflow)
    XTmrCtr_Start(&timer, 0); // starts the timer
    // the timer runs at 100 MHz (same as clk_100), so each tick is 10ns.
    // XPAR_AXI_TIMER_0_BASEADDR is auto-generated to be 0x41C00000 which maps to /axi_timer_0/S_AXI in the Address Editor in Vivado.
    /// ------------------------ END OF MY CODE ------------------------    
    
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
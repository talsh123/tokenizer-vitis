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
// AXI timer driver -- measures tokenizer latency. Kept from the MMIO build so the DMA latency
// numbers are directly comparable to the old byte-by-byte AXI-Lite numbers (same timer, same /100).
#include "xtmrctr.h"
// AXI DMA + cache drivers for the R2 DMA datapath (replaces the byte-by-byte AXI-Lite send/drain).
#include "xaxidma.h"
#include "xil_cache.h"
#include "xparameters.h"

// this timer variable is a global instance of the timer hardware driver.
XTmrCtr timer;

#define TOK_BASE_ADDR  0x44A00000 // tokenizer_axi_lite.v register map -- /tokenizer_axi_lite_0/s_axi base.
// AXI-Lite TOKEN_COUNT register (0x0C): write any value = clear, read = tokens produced since clear.
// AXI DMA Simple-mode S2MM does not report a received length, so we read the count from the IP here.
#define TOK_COUNT_REG  (TOK_BASE_ADDR + 0x0C)

// our AXI DMA base (xparameters.h: XPAR_AXI_DMA_0_BASEADDR = 0x41E10000). NOTE: there are TWO DMAs
// in this design -- ours is 0x41E10000; the AXI Ethernet's own DMA (0x41E00000) is a DIFFERENT
// instance, do not use it. This is the SDT flow, so LookupConfig takes the base address (no device id).
#define TOK_DMA_BASEADDR  XPAR_AXI_DMA_0_BASEADDR

#define MAX_INPUT_BYTES  2048u  // largest text chunk streamed in one DMA transfer
#define MAX_TOKENS       1024u  // largest token response (each token = 2 bytes in DDR)

// DMA buffers -- cache-line (64B) aligned, and they resolve to DDR: the .elf data sections live in
// the MIG range (0x8000_0000..), which is the only window the AXI DMA can reach -- not local BRAM.
static u8  dma_in_buf [MAX_INPUT_BYTES] __attribute__((aligned(64)));
static u16 dma_tok_buf[MAX_TOKENS]      __attribute__((aligned(64)));

static XAxiDma axi_dma;

// one-time DMA bring-up; called from start_application(). returns XST_SUCCESS / XST_FAILURE.
static int tokenizer_dma_init(void)
{
    XAxiDma_Config *cfg = XAxiDma_LookupConfig(TOK_DMA_BASEADDR);
    if (cfg == NULL) { xil_printf("DMA: LookupConfig failed\n\r"); return XST_FAILURE; }
    if (XAxiDma_CfgInitialize(&axi_dma, cfg) != XST_SUCCESS) {
        xil_printf("DMA: CfgInitialize failed\n\r"); return XST_FAILURE;
    }
    if (XAxiDma_HasSg(&axi_dma)) {            // we built Simple mode -- Scatter-Gather must be off
        xil_printf("DMA: unexpected Scatter-Gather mode\n\r"); return XST_FAILURE;
    }
    XAxiDma_IntrDisable(&axi_dma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DEVICE_TO_DMA);
    XAxiDma_IntrDisable(&axi_dma, XAXIDMA_IRQ_ALL_MASK, XAXIDMA_DMA_TO_DEVICE);
    return XST_SUCCESS;
}

// tokenize `len` bytes already sitting in dma_in_buf. The input MUST end on a word boundary
// (whitespace/CR/LF) so the final word flushes and the IP asserts m_axis TLAST on its last token --
// otherwise the S2MM transfer never sees TLAST and the waits below would hang. Returns the number
// of tokens (now in dma_tok_buf), or -1 on error/timeout.
static int tokenizer_dma_run(u32 len)
{
    u32 timeout;
    if (len == 0u || len > MAX_INPUT_BYTES) return -1;

    Xil_Out32(TOK_COUNT_REG, 0u);            // clear the IP's token counter for this transfer

    Xil_DCacheFlushRange((UINTPTR)dma_in_buf, len);
    Xil_DCacheInvalidateRange((UINTPTR)dma_tok_buf, MAX_TOKENS * sizeof(u16));

    // arm the receive (S2MM) FIRST so it's ready before any token is produced, then kick MM2S.
    if (XAxiDma_SimpleTransfer(&axi_dma, (UINTPTR)dma_tok_buf,
            MAX_TOKENS * sizeof(u16), XAXIDMA_DEVICE_TO_DMA) != XST_SUCCESS) return -1;
    if (XAxiDma_SimpleTransfer(&axi_dma, (UINTPTR)dma_in_buf,
            len, XAXIDMA_DMA_TO_DEVICE) != XST_SUCCESS) return -1;

    // wait for the input to finish streaming in...
    timeout = 1000000u;
    while (XAxiDma_Busy(&axi_dma, XAXIDMA_DMA_TO_DEVICE) && --timeout) { }
    if (timeout == 0u) { xil_printf("DMA: MM2S timeout\n\r"); return -1; }

    // ...then for every token to stream back (S2MM completes when the IP asserts TLAST). The caller
    // only launches a transfer when >=1 token is guaranteed, so a timeout here is a real fault.
    timeout = 1000000u;
    while (XAxiDma_Busy(&axi_dma, XAXIDMA_DEVICE_TO_DMA) && --timeout) { }
    if (timeout == 0u) { xil_printf("DMA: S2MM timeout\n\r"); return -1; }

    Xil_DCacheInvalidateRange((UINTPTR)dma_tok_buf, MAX_TOKENS * sizeof(u16));
    return (int)(Xil_In32(TOK_COUNT_REG) & 0xFFFFu);  // the IP reports how many tokens it produced
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
// we removed the template recv_callback and replaced it. This version streams the received text
// through the tokenizer over AXI DMA (optimization R2) instead of byte-by-byte AXI-Lite MMIO: the
// CPU just hands DDR buffers to the DMA and waits, so the ~140 us/token software tax of the old
// poll-write-poll-read loop is gone.
//
// NOTE on cross-segment word reassembly: the previous MMIO version held a word that ended exactly
// on a TCP segment edge in the pipeline until the rest arrived (the M3 fix). This DMA version
// treats each segment as self-contained -- if a segment does not already end on a word boundary it
// appends a newline so the last word flushes and S2MM sees TLAST. For telnet / line-buffered input
// (each line, including its CR/LF, arrives in one segment) the result is identical; only a word
// deliberately split across two TCP segments would tokenize differently. Cross-segment reassembly
// can be re-added later by buffering received bytes until a boundary before launching the DMA.
err_t recv_callback(void *arg, struct tcp_pcb *tpcb,
                               struct pbuf *p, err_t err)
{
    int i;
    u8 *payload;
    char resp_buf[2048];
    int resp_len = 0;
    int ntok;
    u32 len;

    if (!p) {
        tcp_close(tpcb);
        tcp_recv(tpcb, NULL);
        return ERR_OK;
    }

    // tcp_recved tells lwIP we've consumed p->len bytes from the TCP receive window. without it,
    // lwIP would eventually stop accepting data because it thinks its receive buffer is full.
    tcp_recved(tpcb, p->len);
    payload = (u8 *)p->payload;

    // copy the segment into the DDR DMA input buffer (truncate if it somehow exceeds the buffer).
    len = (p->len > MAX_INPUT_BYTES) ? MAX_INPUT_BYTES : (u32)p->len;
    memcpy(dma_in_buf, payload, len);

    // scan the segment: count text chars (debug) AND detect whether it contains any WORD character.
    // this is critical: a segment with NO word char (a lone "\r\n", spaces, etc.) cannot produce a
    // token, and because m_axis_tlast rides on a token, the IP would never assert TLAST -> the S2MM
    // transfer would hang for the whole timeout, stalling lwIP and aborting the connection. So we
    // must NOT launch the DMA on boundary-only input.
    int text_bytes = 0, has_word = 0, has_nl = 0;
    for (i = 0; i < (int)len; i++) {
        u8 c = dma_in_buf[i];
        if (c == '\r' || c == '\n') has_nl = 1; else text_bytes++;
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')) has_word = 1;
    }

    // boundary-only segment: telnet sends a word and its trailing CR/LF as separate TCP segments, so
    // this is the "\r\n" that follows a word. Nothing to tokenize -- skip the DMA. If it carried a
    // line ending, emit a CR/LF so the preceding segment's tokens get terminated on their own line.
    if (!has_word) {
        if (has_nl && (int)tcp_sndbuf(tpcb) >= 2)
            tcp_write(tpcb, "\r\n", 2, 1);
        xil_printf("Received %d bytes (boundary-only, skipped)\n\r", p->len);
        pbuf_free(p);
        return ERR_OK;
    }

    // did this segment end on a real word boundary? (used for the response line framing below)
    u8 last = dma_in_buf[len - 1];
    int ended_on_boundary =
        !((last >= 'a' && last <= 'z') || (last >= 'A' && last <= 'Z') || (last >= '0' && last <= '9'));

    // the segment has >=1 word char, so it WILL produce >=1 token (and thus TLAST). If it didn't
    // already end on a boundary, append a newline so the final word flushes.
    if (!ended_on_boundary && len < MAX_INPUT_BYTES)
        dma_in_buf[len++] = (u8)'\n';

    // time the whole DMA tokenization with the same 100 MHz timer the MMIO build used, so the
    // before/after numbers are apples-to-apples. ticks are 10ns; /100 converts to microseconds.
    u32 t_start = XTmrCtr_GetValue(&timer, 0);
    ntok = tokenizer_dma_run(len);
    u32 t_end = XTmrCtr_GetValue(&timer, 0);

    if (ntok > 0) {
        for (i = 0; i < ntok; i++)
            if (resp_len < (int)sizeof(resp_buf) - 12)
                resp_len += snprintf(resp_buf + resp_len, sizeof(resp_buf) - resp_len,
                                     "%d ", (u16)dma_tok_buf[i]);
    }

    xil_printf("Received %d bytes (%d text)\n\r", p->len, text_bytes);
    if (ntok < 0) {
        xil_printf("DMA tokenize FAILED\n\r");
    } else {
        u32 total_us = (t_end - t_start) / 100;
        xil_printf("Total tokens: %d\n\r", ntok);
        xil_printf("DMA total: %u us | Tokens: %d\n\r", total_us, ntok);
    }

    /* response framing: append the line terminator only when this segment actually ended a line
       (its last byte was a real word boundary), matching the MMIO build. a segment that ended
       mid-word sends its tokens WITHOUT a newline so the next segment's tokens continue the same
       line; segments that produced no tokens send nothing. */
    if (resp_len > 0) {
        if (ended_on_boundary)
            resp_len += snprintf(resp_buf + resp_len, sizeof(resp_buf) - resp_len, "\r\n");

        if (tcp_sndbuf(tpcb) > resp_len) {
            err = tcp_write(tpcb, resp_buf, resp_len, 1);
        } else {
            xil_printf("no space in tcp_sndbuf\n\r");
        }
    }

    // frees the lwIP packet buffer -- without this you'd leak pbufs and eventually run out.
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

    // bring up the AXI DMA used to stream text in / tokens out (optimization R2). if this fails the
    // tokenizer can't run, so make the failure loud rather than hanging later inside recv_callback.
    if (tokenizer_dma_init() != XST_SUCCESS)
        xil_printf("WARNING: tokenizer DMA init failed -- tokenization will not work\n\r");
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
#include "lantask.h"
#include "parser.h"


uint8_t g_dat_buf[DATA_BUF_SIZE];
uint8_t sn = 0;
static uint16_t g_tcp_port = TCP_PORT;

void lan_set_tcp_port(uint16_t port)
{
    if (port != 0u) {
        g_tcp_port = port;
    }
}

uint16_t lan_get_tcp_port(void)
{
    return g_tcp_port;
}

void lan_reopen_listener(void)
{
    disconnect(sn);
    close(sn);
    socket(sn, Sn_MR_TCP, g_tcp_port, 0x00);
    listen(sn);
}


#if 0
void run_tcp_server(volatile bool *flg) {
    uint8_t ir = getSn_IR(sn);
    setSn_IR(sn, ir);      //  Clear Socket-specific interrupt bits
    setSIR(0x01);
    uint8_t current_state = getSn_SR(sn);
    uint16_t len;
    uart1_print("tcp_task_reached\r\n");
    switch(current_state) {
        case SOCK_ESTABLISHED:
        case SOCK_CLOSE_WAIT: 
            len = getSn_RX_RSR(sn);
            if (len > 0) {
                if (len > DATA_BUF_SIZE) len = DATA_BUF_SIZE;
                recv(sn, g_dat_buf, len);
                
                setSn_IR(sn, 0xFF); // Clear W5500 interrupt
                uint16_t parse_len = len;
                if (parse_len > 0 && g_dat_buf[parse_len - 1] == 'e') {
                    parse_len--;
                }
                parse_command(&g_dat_buf[0], parse_len);
                
                uart1_print("Data parsed!\r\n");
                
           
                //if(current_state == SOCK_ESTABLISHED) {
                    send(sn, (uint8_t*)"OK", 2);
                //}
            }

           // if (current_state == SOCK_CLOSE_WAIT) {
                uart1_print("Socket closing cleanup...\r\n");
                disconnect(sn);
                close(sn);
                // Re-open for next connection
                socket(sn, Sn_MR_TCP, g_tcp_port, 0x00);
                listen(sn);
            //}
            
            *flg = false; 
            break;

        case SOCK_CLOSED:
            socket(sn, Sn_MR_TCP, g_tcp_port, 0x00);
            break;

        case SOCK_INIT:
            listen(sn);
            break;

        case SOCK_LISTEN:
            *flg = false; 
            break;

        default:
            *flg = false;
            break;
    }
}
#else

void run_tcp_server(volatile bool *flg) {
    uint8_t ir = getSn_IR(sn);
    setSn_IR(sn, ir);      // Clear Socket-specific interrupt bits
    setSIR(0x01);
    uint8_t current_state = getSn_SR(sn);
    uint16_t len;
    
   switch(getSn_SR(sn))
{
case SOCK_ESTABLISHED:
case SOCK_CLOSE_WAIT:

    len = getSn_RX_RSR(sn);

    if(len)
    {
        if(len > DATA_BUF_SIZE)
            len = DATA_BUF_SIZE;

        recv(sn, g_dat_buf, len);

        setSn_IR(sn, Sn_IR_RECV);

        parse_command(g_dat_buf, len);

       // if(getSn_SR(sn) == SOCK_ESTABLISHED)
            send(sn, (uint8_t *)"OK", 2);
    }

    if(getSn_SR(sn) == SOCK_CLOSE_WAIT)
    {
        disconnect(sn);
        close(sn);
    }

    break;

case SOCK_CLOSED:
    socket(sn, Sn_MR_TCP, g_tcp_port, 0);
    break;

case SOCK_INIT:
    listen(sn);
    break;
}
}

#endif

// --- Support Functions ---

void hardware_reset(void) {
    RCC->APB2ENR |= RCC_APB2ENR_IOPAEN;
    W5500_RST_PORT->CRL &= ~(0xF << (W5500_RST_PIN * 4));
    W5500_RST_PORT->CRL |= (0x3 << (W5500_RST_PIN * 4)); 

    W5500_RST_PORT->BRR = (1 << W5500_RST_PIN); 
    delay_ms(50);
    W5500_RST_PORT->BSRR = (1 << W5500_RST_PIN); 
    delay_ms(100); 
}

void w5500_port_init(void) {
    reg_wizchip_cs_cbfunc(wizchip_select, wizchip_deselect);
    reg_wizchip_spi_cbfunc(spi_read, spi_write);
    reg_wizchip_spiburst_cbfunc(spi_read_burst, spi_write_burst);
}

static void delay_ms(uint32_t ms) {
    while (ms--) {
        for (volatile uint32_t cycles = 0; cycles < 9000; ++cycles) __asm("nop");
    }
}
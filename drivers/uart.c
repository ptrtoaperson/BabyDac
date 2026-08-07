#include "stm32f1xx.h"
#include "system_stm32f1xx.h"
#include "uart.h"


#define Perpher_CLK  72000000//
#define Baudrate	115200


/*
static uint16_t compute_uart_bd(uint32_t PeriphClk, uint32_t BaudRate)
{
	return ((PeriphClk + (BaudRate/2U))/BaudRate);
}
*/

static uint16_t compute_uart_bd(uint32_t PeriphClk, uint32_t BaudRate)
{
    uint32_t usartdiv_mult100 = (25 * PeriphClk) / (4 * BaudRate); // 100 * USARTDIV
    uint32_t mantissa = usartdiv_mult100 / 100;
    uint32_t fraction = ((usartdiv_mult100 - (mantissa * 100)) * 16 + 50) / 100;
    return (mantissa << 4) | (fraction & 0x0F);
}

static void uart_set_baudrate(USART_TypeDef *USARTx, uint32_t PeriphClk,  uint32_t BaudRate)
{
	USARTx->BRR = compute_uart_bd(PeriphClk,BaudRate);
}





void uart1_write(int ch)
{
  /*Make sure the transmit data register is empty*/
	while(!(USART1->SR & USART_SR_TXE)){}

  /*Write to transmit data register*/
	USART1->DR	=  (ch & 0xFF);
}





char uart1_read(void) {

	while(!(USART1->SR& USART_SR_RXNE)) {}
	return USART1->DR;
}

static void (*uart1_rx_callback)(char) = NULL;

void uart1_set_rx_callback(void (*callback)(char)) {
    uart1_rx_callback = callback;
}






int uart1_init(void)
{

	/*UART1 Pin configures*/

	//enable clock access to GPIOA
	RCC->APB2ENR|=RCC_APB2ENR_IOPAEN;
	//Enable clock access to alternate function
	RCC->APB2ENR|=RCC_APB2ENR_AFIOEN;

	/*Confgiure PA9 as output maximum speed to 50MHz
	 * and alternate output push-pull mode*/
	GPIOA->CRH|=GPIO_CRH_MODE9;

	GPIOA->CRH|=GPIO_CRH_CNF9_1;
	GPIOA->CRH&=~GPIO_CRH_CNF9_0;
	
	//configure PA10 for input
	
	GPIOA->CRH &= ~(GPIO_CRH_MODE10);

	GPIOA->CRH|=GPIO_CRH_CNF10_0;
	GPIOA->CRH &=~GPIO_CRH_CNF10_1;
	
	

	

	/*Don't remap the pins*/
	AFIO->MAPR&=~AFIO_MAPR_USART1_REMAP;




	//enable clock access to USART1

	RCC->APB2ENR|=RCC_APB2ENR_USART1EN;

	//Transmit Enable
	USART1->CR1 |= USART_CR1_TE;
	//Enable reciever
	USART1->CR1 |= USART_CR1_RE;
	// Enable RX interrupt
	USART1->CR1 |= USART_CR1_RXNEIE;
	/*Set baudrate */
	uart_set_baudrate(USART1,Perpher_CLK,Baudrate);
	//Enable UART
	USART1->CR1 |= USART_CR1_UE;

	// Enable USART1 interrupt in NVIC
	NVIC_EnableIRQ(USART1_IRQn);

return 0;

}

void USART1_IRQHandler(void) {
    if (USART1->SR & USART_SR_RXNE) {
        char c = USART1->DR;
        if (uart1_rx_callback) {
            uart1_rx_callback(c);
        }
    }
}

// Additional helper functions
void uart1_print(const char *str) {
    while (*str) {
        uart1_write(*str++);
    }
}

void uart1_print_hex(uint8_t value) {
    const char hex_digits[] = "0123456789ABCDEF";
    uart1_write('0');
    uart1_write('x');
    uart1_write(hex_digits[(value >> 4) & 0x0F]);
    uart1_write(hex_digits[value & 0x0F]);
    uart1_write('\n');
}


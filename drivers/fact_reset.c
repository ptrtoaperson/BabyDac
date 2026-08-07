#include "fact_reset.h"

#define FACT_RESET_MARKER_VALUE 0xA55Au
#define FACT_RESET_STREAK_REG   DR1
#define FACT_RESET_MARKER_REG   DR2
#define FACT_RESET_TOTAL_REG    DR3
#define FACT_RESET_START_LO_REG DR4
#define FACT_RESET_START_HI_REG DR5
#define FACT_RESET_WINDOW_SEC   20u

static uint32_t g_last_delta_sec = 0u;
static uint8_t g_last_was_pin_reset = 0u;

static void fact_reset_wait_rtc_write_ready(void)
{
    while ((RTC->CRL & RTC_CRL_RTOFF) == 0u) {
    }
}

static void fact_reset_wait_rtc_sync(void)
{
    RTC->CRL &= (uint16_t)~RTC_CRL_RSF;
    while ((RTC->CRL & RTC_CRL_RSF) == 0u) {
    }
}

static void fact_reset_rtc_enable_lsi_1hz(void)
{
    if ((RCC->CSR & RCC_CSR_LSIRDY) == 0u) {
        RCC->CSR |= RCC_CSR_LSION;
        while ((RCC->CSR & RCC_CSR_LSIRDY) == 0u) {
        }
    }

    if ((RCC->BDCR & RCC_BDCR_RTCEN) == 0u) {
        uint32_t bdcr = RCC->BDCR;
        bdcr &= ~RCC_BDCR_RTCSEL;
        bdcr |= RCC_BDCR_RTCSEL_LSI;
        bdcr |= RCC_BDCR_RTCEN;
        RCC->BDCR = bdcr;
    }
}

static void fact_reset_rtc_init_counter(void)
{
    fact_reset_rtc_enable_lsi_1hz();
    fact_reset_wait_rtc_sync();
    fact_reset_wait_rtc_write_ready();

    RTC->CRL |= RTC_CRL_CNF;
    RTC->PRLH = 0u;
    RTC->PRLL = 39999u;
    RTC->CNTH = 0u;
    RTC->CNTL = 0u;
    RTC->CRL &= (uint16_t)~RTC_CRL_CNF;

    fact_reset_wait_rtc_write_ready();
}

static uint32_t fact_reset_rtc_now_sec(void)
{
    uint32_t cnt;

    fact_reset_rtc_enable_lsi_1hz();
    fact_reset_wait_rtc_sync();
    cnt = ((uint32_t)RTC->CNTH << 16) | (uint32_t)RTC->CNTL;
    return cnt;
}

static void fact_reset_store_start_sec(uint32_t sec)
{
    BKP->FACT_RESET_START_LO_REG = (uint16_t)(sec & 0xFFFFu);
    BKP->FACT_RESET_START_HI_REG = (uint16_t)((sec >> 16) & 0xFFFFu);
}

static uint32_t fact_reset_load_start_sec(void)
{
    return ((uint32_t)BKP->FACT_RESET_START_HI_REG << 16) |
           (uint32_t)BKP->FACT_RESET_START_LO_REG;
}

uint16_t Increment_Reset_Counter(uint8_t clr) {
    // Enable Clock for PWR and BKP peripherals
    RCC->APB1ENR |= (RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN);

    // Disable Backup Domain write protection (Unlock)
    PWR->CR |= PWR_CR_DBP;

    // Read, Increment, and Write back to Backup Data Register 1
    // STM32F103 has 10/42 16-bit backup registers (DR1 to DR42)


    uint16_t count = BKP->FACT_RESET_STREAK_REG;
    uint16_t total = BKP->FACT_RESET_TOTAL_REG;
    uint32_t now_sec;
    uint32_t start_sec;
    uint32_t elapsed_sec;
    uint8_t pin_reset;

    now_sec = fact_reset_rtc_now_sec();
    pin_reset = ((RCC->CSR & RCC_CSR_PINRSTF) != 0u) ? 1u : 0u;
    g_last_was_pin_reset = pin_reset;
    g_last_delta_sec = 0u;

    // Initialize backup registers once after a true backup-domain reset.
    if (BKP->FACT_RESET_MARKER_REG != FACT_RESET_MARKER_VALUE) {
        BKP->FACT_RESET_MARKER_REG = FACT_RESET_MARKER_VALUE;
        count = 0u;
        total = 0u;
        BKP->FACT_RESET_STREAK_REG = count;
        BKP->FACT_RESET_TOTAL_REG = total;
        fact_reset_rtc_init_counter();
        now_sec = fact_reset_rtc_now_sec();
        fact_reset_store_start_sec(now_sec);
    }

    if (clr == 1u) {
        count = 0u;
        BKP->FACT_RESET_STREAK_REG = 0u;
        g_last_delta_sec = 0u;
        fact_reset_store_start_sec(now_sec);
    } else if (pin_reset != 0u) {
        start_sec = fact_reset_load_start_sec();
        elapsed_sec = now_sec - start_sec;
        g_last_delta_sec = elapsed_sec;

        if (total < 0xFFFFu) {
            total = (uint16_t)(total + 1u);
        }
        BKP->FACT_RESET_TOTAL_REG = total;

        if (count == 0u) {
            count = 1u;
            fact_reset_store_start_sec(now_sec);
        } else if (elapsed_sec <= FACT_RESET_WINDOW_SEC) {
            if (count < 0xFFFFu) {
                count = (uint16_t)(count + 1u);
            }
        } else {
            count = 1u;
            fact_reset_store_start_sec(now_sec);
        }

        BKP->FACT_RESET_STREAK_REG = count;
    } else {
        // Ignore non-pin-reset events for gesture counting.
        count = 0u;
        BKP->FACT_RESET_STREAK_REG = 0u;
        g_last_delta_sec = 0u;
        fact_reset_store_start_sec(now_sec);
    }
    // (Optional) Re-enable protection
    PWR->CR &= ~PWR_CR_DBP;

    // Clear reset flags for any other diagnostics that may use RCC_CSR.
    RCC->CSR |= RCC_CSR_RMVF;
    return count;
}

uint16_t Get_Total_Reset_Presses(void)
{
    uint16_t total;

    RCC->APB1ENR |= (RCC_APB1ENR_PWREN | RCC_APB1ENR_BKPEN);
    total = BKP->FACT_RESET_TOTAL_REG;
    return total;
}

uint32_t Get_Reset_Last_Delta_Sec(void)
{
    return g_last_delta_sec;
}

uint32_t Get_Reset_Window_Sec(void)
{
    return FACT_RESET_WINDOW_SEC;
}

uint8_t Was_Last_Reset_Pin(void)
{
    return g_last_was_pin_reset;
}
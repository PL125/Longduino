#include <gd32vf103.h>
#include <stdlib.h>
#include "TInterrupts.h"

typedef struct _ticker_handler_list_t {
    uint8_t tickId;
    uint32_t tickCh;
    void* handler;
    uint32_t microseconds;
    void* param;
    struct _ticker_handler_list_t* next;
} ticker_handler_list_t;

static ticker_handler_list_t* ticker_handler_list = 0;

static const IRQn_Type TIMER_IRQ_MAP[7] = {
    TIMER0_Channel_IRQn,
    TIMER1_IRQn,
    TIMER2_IRQn,
    TIMER3_IRQn,
    TIMER4_IRQn,
    TIMER5_IRQn,
    TIMER6_IRQn,
};

static const uint32_t TIMER_INT_CH_MAP[4] = {
    TIMER_INT_CH0,
    TIMER_INT_CH1,
    TIMER_INT_CH2,
    TIMER_INT_CH3,
};

#define TIMER_HANDLER_ATTR

#define tickerIdToIRQn(t) TIMER_MAP[t]->timer_dev
#define tickerChToIntCh(c) TIMER_INT_CH_MAP[c]
#define tickerIdToTimerId(t) TIMER_MAP[t]->timer_dev

static void attachTickerInternal(ticker_handler_list_t* ptr, uint8_t tickId,
    void* callback, uint32_t microseconds, void* param) {
    uint32_t timerCh = TIMER_CH_0;

    uint32_t prescaler = 108;
    uint32_t period = microseconds;

    while (period >= 100000)
    {
       prescaler *= 10;
       period /= 10;
    }

    /* enable and set key TIMER interrupt to the lowest priority */
    eclic_global_interrupt_enable();
    eclic_irq_enable(TIMER_IRQ_MAP[tickId],1, 1);

    /* ----------------------------------------------------------------------------
    TIMER1 Configuration: 
    TIMER1CLK = SystemCoreClock/108 = 1MHz.
    TIMER1 configuration is timing mode, and the timing is 0.2s(4000/20000 = 0.2s).
    CH0 update rate = TIMER1 counter clock/CH0CV = 20000/4000 = 5Hz.
    ---------------------------------------------------------------------------- */
    timer_oc_parameter_struct timer_ocinitpara;
    timer_parameter_struct timer_initpara;

    rcu_periph_clock_enable(TIMER_MAP[tickId]->clk_id);

    timer_deinit(tickerIdToTimerId(tickId));
    /* initialize TIMER init parameter struct */
    timer_struct_para_init(&timer_initpara);
    /* TIMER configuration */
    timer_initpara.prescaler         = prescaler-1; // 107;
    timer_initpara.alignedmode       = TIMER_COUNTER_EDGE;
    timer_initpara.counterdirection  = TIMER_COUNTER_UP;
    timer_initpara.period            = period; // microseconds;
    timer_initpara.clockdivision     = TIMER_CKDIV_DIV1;
    timer_init(tickerIdToTimerId(tickId), &timer_initpara);

    /* initialize TIMER channel output parameter struct */
    timer_channel_output_struct_para_init(&timer_ocinitpara);
    /* CH0,CH1 and CH2 configuration in OC timing mode */
    timer_ocinitpara.outputstate  = TIMER_CCX_ENABLE;
    timer_ocinitpara.ocpolarity   = TIMER_OC_POLARITY_HIGH;
    timer_ocinitpara.ocidlestate  = TIMER_OC_IDLE_STATE_LOW;
    timer_channel_output_config(tickerIdToTimerId(tickId), timerCh, &timer_ocinitpara);

    /* CH0 configuration in OC timing mode */
    timer_channel_output_pulse_value_config(tickerIdToTimerId(tickId), timerCh, timer_initpara.period/2);
    timer_channel_output_mode_config(tickerIdToTimerId(tickId), timerCh, TIMER_OC_MODE_TIMING);
    timer_channel_output_shadow_config(tickerIdToTimerId(tickId), timerCh, TIMER_OC_SHADOW_DISABLE);

    timer_interrupt_flag_clear(tickerIdToIRQn(tickId), tickerChToIntCh(timerCh));

    ptr->handler = callback;
    ptr->tickId = tickId;
    ptr->tickCh = timerCh;
    ptr->microseconds = microseconds;
    ptr->param = param;

    timer_interrupt_enable(tickerIdToIRQn(tickId), tickerChToIntCh(timerCh));
    timer_enable(tickerIdToTimerId(tickId));
}

void attachTicker(uint8_t tickId, voidFuncPtr callback, uint32_t microseconds) {
    attachTickerParam(tickId, (voidFuncPtrParam)callback, microseconds, 0);
}

void attachTickerParam(
    uint8_t tickId, voidFuncPtrParam callback, uint32_t microseconds, void* param) {
    if (tickId >= VARIANT_TIMER_NUM) {
        return;
    }
    if (callback == 0) {
        return;
    }
    if (ticker_handler_list == 0) {
        ticker_handler_list = calloc(1, sizeof(ticker_handler_list_t));
        attachTickerInternal(ticker_handler_list, tickId, callback, microseconds, param);
        ticker_handler_list->next = 0;
        return;
    }
    ticker_handler_list_t* ptr = ticker_handler_list;
    ticker_handler_list_t* previousPtr = ticker_handler_list;
    while (ptr != 0) {
        if (ptr->tickId == tickId) {
            attachTickerInternal(ptr, tickId, callback, microseconds, param);
            return;
        }
        previousPtr = ptr;
        ptr = ptr->next;
    }
    previousPtr->next = calloc(1, sizeof(ticker_handler_list_t));
    ptr = previousPtr->next;
    attachTickerInternal(ptr, tickId, callback, microseconds, param);
    ptr->next = 0;
    return;
}

void detachTicker(uint8_t tickId) {
    if (ticker_handler_list == 0) {
        return;
    }
    if (ticker_handler_list->tickId == tickId) {
        timer_interrupt_disable(tickerIdToIRQn(tickId), tickerChToIntCh(ticker_handler_list->tickCh));
        if (ticker_handler_list->next == 0) {
            free(ticker_handler_list);
            ticker_handler_list = 0;
            return;
        }
        else {
            ticker_handler_list_t* ptr = ticker_handler_list;
            ticker_handler_list = ticker_handler_list->next;
            free(ptr);
            return;
        }
    }
    ticker_handler_list_t* ptr = ticker_handler_list->next;
    ticker_handler_list_t* previousPtr = ticker_handler_list;
    while (ptr != 0) {
        if (ptr->tickId == tickId) {
            timer_interrupt_disable(tickerIdToIRQn(tickId), tickerChToIntCh(ptr->tickCh));
            previousPtr->next = ptr->next;
            free(ptr);
            return;
        }
        previousPtr = ptr;
        ptr = ptr->next;
    }
    return;
}

static void timer_generic_IRQHandler(void) {
    ticker_handler_list_t* ptr = ticker_handler_list;
    while (ptr != 0) {
        if (timer_interrupt_flag_get(tickerIdToIRQn(ptr->tickId), tickerChToIntCh(ptr->tickCh)) != RESET) {
            if (ptr->param == 0) {
                ((voidFuncPtr)ptr->handler)();
            } else {
                ((voidFuncPtrParam)ptr->handler)(ptr->param);
            }
            timer_interrupt_flag_clear(tickerIdToIRQn(ptr->tickId), tickerChToIntCh(ptr->tickCh));
            break;
        }
        ptr = ptr->next;
    }
    return;
}

void TIMER_HANDLER_ATTR TIMER0_Channel_IRQHandler(void) { timer_generic_IRQHandler(); }
void TIMER_HANDLER_ATTR TIMER1_IRQHandler(void) { timer_generic_IRQHandler(); }
void TIMER_HANDLER_ATTR TIMER2_IRQHandler(void) { timer_generic_IRQHandler(); }
void TIMER_HANDLER_ATTR TIMER3_IRQHandler(void) { timer_generic_IRQHandler(); }
void TIMER_HANDLER_ATTR TIMER4_IRQHandler(void) { timer_generic_IRQHandler(); }
void TIMER_HANDLER_ATTR TIMER5_IRQHandler(void) { timer_generic_IRQHandler(); }
void TIMER_HANDLER_ATTR TIMER6_IRQHandler(void) { timer_generic_IRQHandler(); }

#include <MKL25Z4.h>

// --- CONFIGURATION ---
// BIT-BANG METHOD (No SPI)
// Tuned for 21 MHz Core Clock
// ---------------------

#define LED_PIN_MASK (1UL << 2) // PTD2

void Delay(volatile uint32_t cnt) {
    while(cnt--) __asm("nop");
}

void Init_GPIO(void) {
    // Enable Clock to Port D
    SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK;
    
    // Set PTD2 to GPIO Mode (Mux Alt 1)
    PORTD->PCR[2] = PORT_PCR_MUX(1);
    
    // Set PTD2 as Output
    PTD->PDDR |= LED_PIN_MASK;
    
    // Initialize Low (Reset State)
    PTD->PCOR = LED_PIN_MASK;
}

// Precise delay for timing pulses
// At 21MHz, 1 cycle = ~47ns
// We need ~350-400ns High for '0'
// We need ~700-800ns High for '1'
__attribute__((always_inline)) static inline void NOP_Delay(int cycles) {
    while(cycles--) __asm("nop");
}

void Send_Pixel(uint8_t r, uint8_t g, uint8_t b) {
    // Standard WS2812B order is GRB
    uint8_t bytes[3] = {g, r, b};
    
    // Disable interrupts so nothing interrupts our timing
    __disable_irq();

    for (int i = 0; i < 3; i++) {
        uint8_t byte = bytes[i];
        for (int bit = 7; bit >= 0; bit--) {
            if (byte & (1 << bit)) {
                // --- SEND '1' ---
                // High for ~800ns, Low for ~450ns
                PTD->PSOR = LED_PIN_MASK; // Set High
                NOP_Delay(10);            // Wait (Tuned for 21MHz)
                PTD->PCOR = LED_PIN_MASK; // Set Low
                NOP_Delay(1);             // Wait
            } else {
                // --- SEND '0' ---
                // High for ~400ns, Low for ~850ns
                PTD->PSOR = LED_PIN_MASK; // Set High
                NOP_Delay(2);             // Wait (Short pulse)
                PTD->PCOR = LED_PIN_MASK; // Set Low
                NOP_Delay(10);            // Wait
            }
        }
    }
    
    __enable_irq();
}

int main(void) {
    Init_GPIO();
    
    // Force a clean start (Low > 50us)
    Delay(5000); 

    while(1) {
        // Red
        Send_Pixel(50, 0, 0); 
        Delay(5000000); // 5M loops = visible delay
        
        // Green
        Send_Pixel(0, 50, 0); 
        Delay(5000000);
        
        // Blue
        Send_Pixel(0, 0, 50); 
        Delay(5000000);
        
        // Yellow (Red + Green) - Test color mixing
        Send_Pixel(50, 50, 0);
        Delay(5000000);
    }
}
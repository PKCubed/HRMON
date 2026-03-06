/*
Peter Kyle

ENGR355 Embedded Systems - Winter 2026
Final Project

Heart Rate Monitor

*/
# include <MKL25Z4.h>
# include <string.h>
# include <stdbool.h>

#define MASK(x) (1UL << (x))

// Pin Definitions
#define LCD_EN 17 // PTC17 LCD Enable (Clock)
#define LCD_RW 16 // PTC16 LCD Read/Write
#define LCD_RS 13 // PTC13 LCD Register Select

// LCD Data pins are PTC0-PTC3

#define LCD_BL 4 // PTC4 Backlight


// Buttons
#define BUTTON_UP 5 // PTA5
#define BUTTON_LEFT 12 // PTA12
#define BUTTON_RIGHT 13 // PTA13
#define BUTTON_DOWN 14 // PTA14
#define BUTTON_CENTER 15 // PTA15


// Heart shape bitmask
uint8_t STARTUP_LOGO[8][8] = {{
	0b01100, // P
	0b10010,
	0b10010,
	0b10010,
	0b11100,
	0b10000,
	0b10000,
	0b00000
},{
	0b10010, // K
	0b10010,
	0b10010,
	0b10100,
	0b11010,
	0b10010,
	0b10010,
	0b00000
},{
	0b00100, // Space half of H
	0b00100,
	0b00100,
	0b00111,
	0b00100,
	0b00100,
	0b00100,
	0b00000
},{
	0b10011, // End of H, half of R
	0b10100,
	0b10100,
	0b10100,
	0b10111,
	0b10101,
	0b10100,
	0b00000
},{
	0b00001, // End of R, Start of M
	0b10001,
	0b10001,
	0b10001,
	0b00001,
	0b00001,
	0b10001,
	0b00000
},{
	0b10110,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b01010,
	0b00010,
	0b00000
},{
	0b01100, // end of M, Half of O
	0b10010,
	0b10010,
	0b10010,
	0b10010,
	0b10010,
	0b01100,
	0b00000
},{
	0b10001,
	0b11001,
	0b11001,
	0b10101,
	0b10011,
	0b10011,
	0b10001,
	0b00000
}
};

uint8_t LEFT_ARROW[8] = {
	0b00000,
	0b00100,
	0b01100,
	0b11111,
	0b01100,
	0b00100,
	0b00000,
	0b00000
};
uint8_t RIGHT_ARROW[8] = {
	0b00000,
	0b00100,
	0b00110,
	0b11111,
	0b00110,
	0b00100,
	0b00000,
	0b00000
};

void delay(uint32_t count) { // Delay Microseconds (approx)
	while(count--) {}
}

void delay_ms(uint32_t n) {
uint32_t i;
uint32_t j;
for(i=0; i < n; i++)
	for(j=0; j < 3500; j++) {}
}

// Pulses the Enable pin to tell the LCD to read the data lines
void LCD_pulse_enable(void) {
	PTC->PSOR = (1 << LCD_EN); // Set EN High
	delay(12);               // 1us delay
	PTC->PCOR = (1 << LCD_EN); // Set EN Low
	delay(50*12);              // Give LCD time to process the data
}

// Writes 4-bit nibble (0b0000 to 0b1111) to PTC0-PTC3
void LCD_write_nibble(uint8_t nibble) {
	// Clear only the bottom 4 bits (PTC0-PTC3)
	PTC->PCOR = 0x0F; 
	
	// Set the bottom 4 bits with our nibble
	PTC->PSOR = (nibble & 0x0F); 
	
	LCD_pulse_enable();
}

void LCD_send_command(uint8_t cmd) {
	PTC->PCOR = (1 << LCD_RS); // RS = 0 for Command
	PTC->PCOR = (1 << LCD_RW); // RW = 0 for Write
	
	LCD_write_nibble(cmd >> 4); // Send high nibble
	LCD_write_nibble(cmd & 0x0F); // Send low nibble
	
	// For commands that need a longer delay, we have to wait a little longer
	if (cmd == 0x01 || cmd == 0x02) {
		delay_ms(2);
	}
}

void LCD_clear() {
	LCD_send_command(0x01);
}

void LCD_home() {
	LCD_send_command(0x02);
}

void LCD_send_data(uint8_t cmd) {
	PTC->PSOR = (1 << LCD_RS); // RS = 1 for Data
	PTC->PCOR = (1 << LCD_RW); // RW = 0 for Write
	
	LCD_write_nibble(cmd >> 4); // Send high nibble
	LCD_write_nibble(cmd & 0x0F); // Send low nibble
}

void LCD_init() {
	// Enable clock to port C
	SIM->SCGC5 |= SIM_SCGC5_PORTC_MASK;
	
	// Configure pins as GPIO
	PORTC->PCR[0]  = PORT_PCR_MUX(1);
	PORTC->PCR[1]  = PORT_PCR_MUX(1);
	PORTC->PCR[2]  = PORT_PCR_MUX(1);
	PORTC->PCR[3]  = PORT_PCR_MUX(1);
	PORTC->PCR[LCD_BL] = PORT_PCR_MUX(1);
	PORTC->PCR[LCD_RS] = PORT_PCR_MUX(1);
	PORTC->PCR[LCD_RW] = PORT_PCR_MUX(1);
	PORTC->PCR[LCD_EN] = PORT_PCR_MUX(1);
	
	// Setup pins to be outputs
	PTC->PDDR |= 0x0F | (1 << LCD_BL) | (1 << LCD_RS) | (1 << LCD_RW) | (1 << LCD_EN);
	
	// Initialize outputs to low
	PTC->PCOR = 0x0F | (1 << LCD_BL) | (1 << LCD_RS) | (1 << LCD_RW) | (1 << LCD_EN);
	PTC->PSOR = (1 << LCD_BL); // Turn on backlight
	
	delay_ms(50);
	
	// Wake up the display
	LCD_write_nibble(0x03);
	delay_ms(5);
	LCD_write_nibble(0x03);
	delay(150*12);
	LCD_write_nibble(0x03);
	delay(50*12);
	
	// Set to 4-bit mode
	LCD_write_nibble(0x02);
	delay(50*12);
	
	LCD_send_command(0x28); // 4-bit mode, 2 lines, 5x8 font
	LCD_send_command(0x08); // Display off
	LCD_send_command(0x01); // Clear display
	LCD_send_command(0x06); // Entry mode: increment cursor, no shift
	LCD_send_command(0x0C); // Display On, no cursor, no blinking
}

void LCD_send_int(int value) { // Send a single decimal digit
	LCD_send_data(value+48);
}

void LCD_send_single_hex(uint32_t value) {
	if (value < 10) {
		LCD_send_data(value+48);
	} else {
		LCD_send_data(value-10+65);
	}
}

void LCD_send_hex(uint16_t value) {
	for (int i=0; i<4; i++) {
		LCD_send_single_hex((value >> (4*(3-i))) & 0xF);
	}
}

void LCD_send_string(char string[]) {
	//LCD_clear();
	//LCD_home();
	int length = strlen(string);
	for (int i=0; i<length; i++) {
		if (string[i] == '\n') {
			LCD_send_command(0xC0);
		} else {
			LCD_send_data(string[i]);
		}
	}
}

void LCD_create_char(uint8_t location, uint8_t charmap[]) { // Lcation is 0-7 slot to save character in. Charmap is an array of 8 bytes for the pixel grid
    location &= 0x07; // Constrain location to 0-7
    
    // Command 0x40 sets the CGRAM address. 
    // We multiply location by 8 (shift left 3) because each char is 8 bytes.
    LCD_send_command(0x40 | (location << 3)); 
    
    // Send the 8 bytes of the character
    for (int i = 0; i < 8; i++) {
        LCD_send_data(charmap[i]);
    }
}

void LCD_set_cursor(uint8_t col, uint8_t row) {
    uint8_t address;
    if (row == 0) {
        address = col;          // Top row starts at 0x00
    } else {
        address = 0x40 + col;   // Bottom row starts at 0x40
    }

    LCD_send_command(0x80 | address);
}

void PWM_init(void) { // Initialize PWM for LEDs on PTC8, and PTC9. Unfortunately, PTC10 (Green LED) does not support PWM from the TPM.
    
    // Set PTC8 and PTC9 to Alt3 (TPM0_CH4 and TPM0_CH5)
    PORTC->PCR[8] = PORT_PCR_MUX(3);
    PORTC->PCR[9] = PORT_PCR_MUX(3);
    
    // Select the clock source for the TPM modules
    SIM->SOPT2 |= SIM_SOPT2_TPMSRC(1); 
    
    SIM->SCGC6 |= SIM_SCGC6_TPM0_MASK; // Enable clock to the TPM0 module (PTC8 and PTC9 use TPM0, not TPM2)
    
    TPM0->SC = 0; // Disable TPM0 counter while we configure it
    
    TPM0->MOD = 48000; // Set the Modulo (MOD) register for your frequency
    
    // Configure TPM0 Channel 4 (PTC8) and Channel 5 (PTC9) for Edge-Aligned PWM
    TPM0->CONTROLS[4].CnSC = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSB_MASK;
    TPM0->CONTROLS[5].CnSC = TPM_CnSC_MSB_MASK | TPM_CnSC_ELSB_MASK;
    
    // Set initial duty cycle to 0 (off)
    TPM0->CONTROLS[4].CnV = 0; 
    TPM0->CONTROLS[5].CnV = 0; 
    
    // Start the timer counter
    TPM0->SC |= TPM_SC_CMOD(1);
}

void LED_init() {
	PWM_init();
	PORTC->PCR[10] = PORT_PCR_MUX(1); // Setup green led (PTC10) as GPIO
	PTC->PDDR |= (1 << 10); // Setup green led (PTC10) as output
	PTC->PCOR = (1 << 10); // Turn off green led
}

void red_led(uint8_t brightness) {
	// Change brightness of the red LED on PTC8 (Channel 4)
	TPM0->CONTROLS[4].CnV = brightness*48000/255; // 50% duty cycle
}

void yellow_led(uint8_t brightness) {
	// Change brightness of the red LED on PTC8 (Channel 4)
	TPM0->CONTROLS[5].CnV = brightness*48000/255; // 50% duty cycle
}

void green_led(int state) {
	if (state) {
		PTC->PSOR = (1 << 10); // Turn on green led
	} else {
		PTC->PCOR = (1 << 10); // Turn off green led
	}
}

void buttons_init() {
	// Configuration for button pins
	// MUX(1)    = GPIO
	// PE_MASK   = Enable internal pull resistor
	// PS_MASK   = Set pull resistor to Pull-Up (High)
	// IRQC(10)  = Trigger interrupt on Falling Edge (High-to-Low transition)
	uint32_t pcr_setup = PORT_PCR_MUX(1) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK | PORT_PCR_IRQC(10);
	
	// Apply configuration to button pins
	PORTA->PCR[BUTTON_UP]  = pcr_setup;
	PORTA->PCR[BUTTON_LEFT] = pcr_setup;
	PORTA->PCR[BUTTON_RIGHT] = pcr_setup;
	PORTA->PCR[BUTTON_DOWN] = pcr_setup;
	PORTA->PCR[BUTTON_CENTER] = pcr_setup;
	
	// 3. Ensure the pins are set as inputs in the Data Direction Register (PDDR)
	// Clearing the bits to 0 makes them inputs.
	PTA->PDDR &= ~(MASK(BUTTON_UP) | MASK(BUTTON_LEFT) | MASK(BUTTON_RIGHT) | MASK(BUTTON_DOWN) | MASK(BUTTON_CENTER));
	
	// 4. Clear any pending interrupts that might have triggered during setup
	PORTA->ISFR = MASK(BUTTON_UP) | MASK(BUTTON_LEFT) | MASK(BUTTON_RIGHT) | MASK(BUTTON_DOWN) | MASK(BUTTON_CENTER);
	
	// 5. Enable the Port A interrupt in the NVIC
	NVIC_EnableIRQ(PORTA_IRQn);
}

// This name must be exact, as it is defined in the startup code
void PORTA_IRQHandler(void) {
    
    // Check if PTA5 triggered the interrupt
    if (PORTA->ISFR & (1 << 5)) {
        // TODO: Add your PTA5 button logic here
        
        // Clear the interrupt flag
        PORTA->ISFR = (1 << 5); 
    }
    
    // Check if PTA12 triggered the interrupt
    if (PORTA->ISFR & (1 << 12)) {
        // TODO: Add your PTA12 button logic here
        
        // Clear the interrupt flag
        PORTA->ISFR = (1 << 12); 
    }
    
    // Check if PTA13 triggered the interrupt
    if (PORTA->ISFR & (1 << 13)) {
        // TODO: Add your PTA13 button logic here
        PORTA->ISFR = (1 << 13); 
    }
    
    // Check if PTA14 triggered the interrupt
    if (PORTA->ISFR & (1 << 14)) {
        // TODO: Add your PTA14 button logic here
        PORTA->ISFR = (1 << 14); 
    }
    
    // Check if PTA15 triggered the interrupt
    if (PORTA->ISFR & (1 << 15)) {
        // TODO: Add your PTA15 button logic here
        PORTA->ISFR = (1 << 15); 
    }
}

int main(void) {
	// Enable clocks
	SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTC_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK;
	
	LCD_init(); // initialize LCD display
	LED_init();
	buttons_init();
	
	for (int i=0; i<8; i++) {
		LCD_create_char(i, STARTUP_LOGO[i]);
	}
	
	
	LCD_home();
	for (int i=0; i<8; i++) {
		LCD_send_data(i); // Display the startup logo on the first 8 characters
	}
	LCD_set_cursor(0, 1);
	LCD_send_string("Starting");
	
	for (int i=0; i<=240; i++) {
		if (i <= 80) {
			red_led(i);
		} else if (i <= 160) {
			red_led(160-i);
		}
		if (i >= 60 && i <= 140) {
			yellow_led(i-60);
		} else if (i >= 140 && i <= 220) {
			yellow_led(220-i);
		}
		if (i == 120) {
			green_led(1);
		}
		if (i == 240) {
			green_led(0);
		}
		delay_ms(5);
	}
	
	// Load left and right arrow custom characters
	LCD_create_char(0, LEFT_ARROW);
	LCD_create_char(1, RIGHT_ARROW);
	
	LCD_clear();
	LCD_send_string("Measure");
	LCD_set_cursor(0, 1);
	LCD_send_data(0);
	LCD_set_cursor(7, 1);
	LCD_send_data(1);
}


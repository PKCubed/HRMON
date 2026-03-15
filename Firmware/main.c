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

#define DEBOUNCE_DURATION 2000000ULL // Clock Cycles

uint64_t button_up_timer = 0;
uint64_t button_left_timer = 0;
uint64_t button_right_timer = 0;
uint64_t button_down_timer = 0;
uint64_t button_center_timer = 0;

uint8_t button_up_state = 0;
uint8_t button_left_state = 0;
uint8_t button_right_state = 0;
uint8_t button_down_state = 0;
uint8_t button_center_state = 0;

#define DAC_PIN 30

/*
screen:
0  = Restart Option
1  = Credits Option
2  = Measure Option
3  = Settings Option
4  = Test Option

9  = Credits
10 = Measurement Mode
12 = Test

Settings screens:
50 = Save / Back to main menu
51 = Backlight On/Off/Auto
52 = LED Brightness
53 = Low HR Warning
54 = High HR Warning
53 = Factory Reset
*/

int screen = 2;
int last_screen = 2;

#define MENU_OPTIONS 5

char *menu_options_text[] = {
	"Restart ",
  "Credits ",
  "Measure ",
  "Settings",
  "Test    "
};

#define CREDITS_SCROLL_TIME 5500000ULL

#define CREDITS_STR "        Created by Peter Kyle for ENGR355 (Embedded Systems Design) at Walla Walla University   Taught by Dr. Natalie Smith-Gray   Winter 2026        "
#define CREDITS_TEXT_LEN (sizeof(CREDITS_STR) - 1)

const char *credits_text = CREDITS_STR;

#define MENU_OPTION_SLIDE_TIME 1500000ULL

signed int menu_option_slide = 0;
// int last_menu_option_slide = 0;
uint64_t menu_option_slide_timer = 0;

#define TEST_MODE_TIME 10000000ULL



// Created with the help of Gemini v3 Pro
void get_slide_frame(int screen, int menu_option_slide, char *output_buffer) {
	  // Create blank 8-character words
    char prev[9] = "        ";
    char curr[9] = "        ";
    char next[9] = "        ";

    // Safely grab the adjacent strings (if they exist)
    if (screen > 0) {
        strncpy(prev, menu_options_text[screen - 1], 8);
				prev[8] = '\0'; // Guarentee null termination
    }
    strncpy(curr, menu_options_text[screen], 8);
		curr[8] = '\0';
    if (screen < MENU_OPTIONS - 1) {
        strncpy(next, menu_options_text[screen + 1], 8);
				next[8] = '\0';
    }

    // Stitch the individual 8 character menu options together into a 24-character "ribbon"
    // Layout: [Prev (0-7)] [Curr (8-15)] [Next (16-23)]
    char ribbon[25] = ""; 
    strcat(ribbon, prev);
    strcat(ribbon, curr);
    strcat(ribbon, next);

    // Calculate the starting index of our 8-character window.
    // Index 8 is the exact start of the 'Current' word.
    // Adding the slide moves the window left (negative) or right (positive).
    int start_index = 8 + menu_option_slide;

    // Copy exactly 8 characters from the calculated starting point
    strncpy(output_buffer, &ribbon[start_index], 8);
    
    // Manually add the null terminator so it's a valid C string
    output_buffer[8] = '\0';
}

// Tt variable is a 64-bit integer but the KL25Z is a 32-bit processor, it takes two clock cycles to read t. If the systick handler first exactly between those two instructions, it will
// corrupt the read, making things think we jumped 4 billion cycles into the future, permanently breaking timers and things. We can fix this by creating the get_safe_time() function.
volatile uint64_t t_unsafe = 3000000; // Time (clock cycles) since boot. Gets incremented by systick. Start it at 3000000 to fast forward past any debounce timers to prevent initial phantom button presses.
uint64_t t = 3000000;

// This ISR triggers every 10000 clock cycles
void SysTick_Handler(void) {
    t_unsafe+=10000; // Increment by 10000 clock cycles
}

uint64_t get_safe_time() {
    __disable_irq();
		__ISB();
    uint64_t safe_t = t_unsafe;
    __enable_irq();
    return safe_t;
}

void delay(volatile uint32_t count) { // Delay Microseconds (approx)
	while(count--) {}
}

void delay_ms(uint32_t n) {
volatile uint32_t i;
volatile uint32_t j;
for(i=0; i < n; i++)
	for(j=0; j < 3500; j++) {}
}

// APA102 Stuff
void spi0_init() {
	SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK; // Enable clock to PORTD
	SIM->SCGC4 |= SIM_SCGC4_SPI0_MASK; // Enable clock to SPI0
	
	// Setup PTD1 and PTD2 to ALT2 (SPI0)
	PORTD->PCR[1] = PORT_PCR_MUX(2); // PTD1 = SPI0_SCK
	PORTD->PCR[2] = PORT_PCR_MUX(2); // PTD2 = SPI0_SOUT
	
	SPI0->C1 = SPI_C1_MSTR_MASK | SPI_C1_SPE_MASK; // MSTR=1 for master mode, spe=1 for spi system enable
	
	SPI0->BR = SPI_BR_SPPR(2) | SPI_BR_SPR(1); // SPPR=2 (divide by 3), SPR=1 (divide by 4)
}

void spi0_write(uint8_t data) {
	// Wait until the Transmit Buffer Empty flag is set
	while(!(SPI0->S & SPI_S_SPTEF_MASK)) {
			// Wait
	}
	// Writing to the Data register clears the flag and transmits
	SPI0->D = data;
}

void apa102_start_frame(void) {
    spi0_write(0x00);
    spi0_write(0x00);
    spi0_write(0x00);
    spi0_write(0x00);
}

void apa102_end_frame(void) {
    spi0_write(0xFF);
    spi0_write(0xFF);
    spi0_write(0xFF);
    spi0_write(0xFF);
}

// Set a single LED color. Brightness is 0-31.
void apa102_send_led(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness) {
    // Ensure brightness is capped at 5 bits (31 max)
    brightness &= 0x1F; 
    
    // The start byte is 0b11100000 (0xE0) OR'd with the 5-bit brightness
    spi0_write(0xE0 | brightness); 
    
    // APA102 expects colors in BGR order
    spi0_write(b);
    spi0_write(g);
    spi0_write(r);
}

uint8_t rgb_leds[6][3] = {{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0},{0,0,0}}; // Array buffer of LED RGB values
uint8_t rgb_brightness = 31; // 5 bit value

void rgb_refresh() {
	apa102_start_frame();
	for (int i=0; i<6; i++) {
		apa102_send_led(rgb_leds[i][0], rgb_leds[i][1], rgb_leds[i][2], rgb_brightness);
	}
	apa102_end_frame();
}

void rgb_set_led(int led_index, int r, int g, int b) {
	rgb_leds[led_index][0] = r;
	rgb_leds[led_index][1] = g;
	rgb_leds[led_index][2] = b;
}

void rgb_set_leds(int r, int g, int b) {
	for (int i=0; i<6; i++) {
		rgb_leds[i][0] = r;
		rgb_leds[i][1] = g;
		rgb_leds[i][2] = b;
	}
}

// This function was created with the help of Gemini 3 Pro
void set_ring_rainbow_color(int logical_index, uint8_t hue) {
    // Map the logical ring position (0-5) to your physical LED indices
    int ring_map[6] = {2, 1, 0, 3, 4, 5};
    int physical_led = ring_map[logical_index];

    uint8_t r, g, b;
    
    // Basic 0-255 color wheel logic
    hue = 255 - hue; 
    if(hue < 85) {
        r = 255 - hue * 3;
        g = 0;
        b = hue * 3;
    } else if(hue < 170) {
        hue -= 85;
        r = 0;
        g = hue * 3;
        b = 255 - hue * 3;
    } else {
        hue -= 170;
        r = hue * 3;
        g = 255 - hue * 3;
        b = 0;
    }

    // Scale down to your desired max brightness of ~40 out of 255
    r = (r * 40) / 255;
    g = (g * 40) / 255;
    b = (b * 40) / 255;

    rgb_set_led(physical_led, r, g, b);
}



uint64_t lcd_update_timer = 0;

// Custom LCD Characters
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
uint8_t LEFT_ARROW_INV[8] = {
	0b11111,
	0b11011,
	0b10011,
	0b00000,
	0b10011,
	0b11011,
	0b11111,
	0b11111
};
uint8_t RIGHT_ARROW_INV[8] = {
	0b11111,
	0b11011,
	0b11001,
	0b00000,
	0b11001,
	0b11011,
	0b11111,
	0b11111
};

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
		delay_ms(5);
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

// This recursive function was created with the assistance of Gemini v3.
void LCD_send_number(int n) {
	if (n<0) { // Handle negative numbers if necessary
		n = -n;
	}
	if (n/10 > 0) {
		LCD_send_number(n / 10);
	}
	LCD_send_data(n%10 + '0');
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
	uint32_t pcr_setup = PORT_PCR_MUX(1) | PORT_PCR_PE_MASK | PORT_PCR_PS_MASK;
	
	// Apply that configuration to the button pins
	PORTA->PCR[BUTTON_UP]  = pcr_setup;
	PORTA->PCR[BUTTON_LEFT] = pcr_setup;
	PORTA->PCR[BUTTON_RIGHT] = pcr_setup;
	PORTA->PCR[BUTTON_DOWN] = pcr_setup;
	PORTA->PCR[BUTTON_CENTER] = pcr_setup;
	
	// Set button pins as inputs by clearing them to zero
	PTA->PDDR &= ~(MASK(BUTTON_UP) | MASK(BUTTON_LEFT) | MASK(BUTTON_RIGHT) | MASK(BUTTON_DOWN) | MASK(BUTTON_CENTER));
}

void button_up_rising_handler() {
	green_led(1);
}
void button_up_falling_handler() {
	green_led(0);
}
void button_left_rising_handler() {
	if (screen > 0 && screen <= 4) {
		LCD_set_cursor(1, 1);
		LCD_send_data(2);
		if (menu_option_slide == 0) {
			screen--;
			menu_option_slide = 8;
		}
		LCD_set_cursor(6, 1);
		LCD_send_data(1);
	} else if (screen != 12) {
		LCD_set_cursor(1, 1);
		LCD_send_data(' ');
	}
}
void button_left_falling_handler() {
	if (screen > 0 && screen <= 4) {
		LCD_set_cursor(1, 1);
		LCD_send_data(0);
	} else if (screen != 12) {
		LCD_set_cursor(1, 1);
		LCD_send_data(' ');
	}
}
void button_right_rising_handler() {
	if (screen < 4) {
		LCD_set_cursor(6, 1);
		LCD_send_data(3);
		if (menu_option_slide == 0) {
			screen++;
			menu_option_slide = -8;
		}
		LCD_set_cursor(1, 1);
		LCD_send_data(0);
	} else if (screen != 12) {
		LCD_set_cursor(6, 1);
		LCD_send_data(' ');
	}
}
void button_right_falling_handler() {
	if (screen < 4) {
		LCD_set_cursor(6, 1);
		LCD_send_data(1);
	} else if (screen != 12) {
		LCD_set_cursor(6, 1);
		LCD_send_data(' ');
	}
}
void button_down_rising_handler() {
	
}
void button_down_falling_handler() {
	
}
void button_center_rising_handler() {
	// If we're on a menu option, we now want to select it.
	if (screen == 2) { // Measure Mode Menu Option
		screen = 10; // Put us in measurement mode
	} else if (screen == 10) {
		screen = 2;
		yellow_led(0);
		rgb_set_leds(0, 0, 0);
		rgb_refresh();
	} else if (screen == 1) { // Credits Mode Menu Option
		screen = 9;
	} else if (screen == 9) {
		screen = 1;
	} else if (screen == 0) { // Reset menu option
		NVIC_SystemReset();
	} else if (screen == 4) { // Test Mode Menu Option
		screen = 12;
	} else if (screen == 12) {
		screen = 4;
	}
}
void button_center_falling_handler() {
	
}


void button_task() {
	// This task is responsible for pulling buttons, debouncing buttons, updating button state variables, and calling button rising and falling functions
	
	// Poll button values
	if ((PTA->PDIR & MASK(BUTTON_UP)) == 0) { // If button is currently pressed
		button_up_timer = t;
	}
	if ((PTA->PDIR & MASK(BUTTON_LEFT)) == 0) { // If button is currently pressed
		button_left_timer = t;
	}
	if ((PTA->PDIR & MASK(BUTTON_RIGHT)) == 0) { // If button is currently pressed
		button_right_timer = t;
	}
	if ((PTA->PDIR & MASK(BUTTON_DOWN)) == 0) { // If button is currently pressed
		button_down_timer = t;
	}
	if ((PTA->PDIR & MASK(BUTTON_CENTER)) == 0) { // If button is currently pressed
		button_center_timer = t;
	}
	
	if (button_up_timer + DEBOUNCE_DURATION >= t) {
		// This button is currently pressed (debounced)
		if (!button_up_state) { // Was it previosuly released, ie. rising edge?
			button_up_state = 1; // Achnowledge
			button_up_rising_handler(); // Call button function
		}
	} else {
		// This button is not currently pressed (debounced)
		if (button_up_state) { // Was it previously pressed, ie. falling edge?
			button_up_state = 0; // Acknowledge
			button_up_falling_handler(); // Call button function
		}
	}
	if (button_left_timer + DEBOUNCE_DURATION >= t) {
		// This button is currently pressed (debounced)
		if (!button_left_state) { // Was it previosuly released, ie. rising edge?
			button_left_state = 1; // Achnowledge
			button_left_rising_handler(); // Call button function
		}
	} else {
		// This button is not currently pressed (debounced)
		if (button_left_state) { // Was it previously pressed, ie. falling edge?
			button_left_state = 0; // Acknowledge
			button_left_falling_handler(); // Call button function
		}
	}
	if (button_right_timer + DEBOUNCE_DURATION >= t) {
		// This button is currently pressed (debounced)
		if (!button_right_state) { // Was it previosuly released, ie. rising edge?
			button_right_state = 1; // Achnowledge
			button_right_rising_handler(); // Call button function
		}
	} else {
		// This button is not currently pressed (debounced)
		if (button_right_state) { // Was it previously pressed, ie. falling edge?
			button_right_state = 0; // Acknowledge
			button_right_falling_handler(); // Call button function
		}
	}
	if (button_down_timer + DEBOUNCE_DURATION >= t) {
		// This button is currently pressed (debounced)
		if (!button_down_state) { // Was it previosuly released, ie. rising edge?
			button_down_state = 1; // Achnowledge
			button_down_rising_handler(); // Call button function
		}
	} else {
		// This button is not currently pressed (debounced)
		if (button_down_state) { // Was it previously pressed, ie. falling edge?
			button_down_state = 0; // Acknowledge
			button_down_falling_handler(); // Call button function
		}
	}
	if (button_center_timer + DEBOUNCE_DURATION >= t) {
		// This button is currently pressed (debounced)
		if (!button_center_state) { // Was it previosuly released, ie. rising edge?
			button_center_state = 1; // Achnowledge
			button_center_rising_handler(); // Call button function
		}
	} else {
		// This button is not currently pressed (debounced)
		if (button_center_state) { // Was it previously pressed, ie. falling edge?
			button_center_state = 0; // Acknowledge
			button_center_falling_handler(); // Call button function
		}
	}
}

void init_adc(void) {
    // Open clocking gate to the adc
    SIM->SCGC6 |= SIM_SCGC6_ADC0_MASK;

		// Set PTB0 to Analog Mode (MUX = 0)
    PORTB->PCR[0] = PORT_PCR_MUX(0);
	
    // Configure the adc settings
    ADC0->CFG1 = ADC_CFG1_ADICLK(0) | // Use the bus clock
                 ADC_CFG1_MODE(1) | // 12 bit conversion
                 ADC_CFG1_ADIV(2) | // Divide clock by 4
								 ADC_CFG1_ADLSMP_MASK; // Long sample time (set this bit)

    // Reset all bits to use software trigger, no compare, and default reference voltages
    ADC0->SC2 = 0;
}

uint16_t adc_read(uint8_t channel) {
	ADC0->SC1[0] = channel & ADC_SC1_ADCH_MASK; // Start a conversion on the desired channel
	
	while (!(ADC0->SC1[0] & ADC_SC1_COCO_MASK)); // Wait for the COCO flag. Do nothing in the mean time
	
	return (uint16_t) ADC0->R[0]; // Cast ADC return value to 16 bit unsigned integer before returning.
}

int bpm_active = 0;
uint64_t last_rising_edge_t = 0;
int period = 0;
int period_ms = 0;
int bpm = 0;
int last_bpm = 0;
int adc_value_state = 0;

#define UPPER_THRESHOLD 2110 // 1.700 V (1.700/3.3*4096 = 2110.06)
#define LOWER_THRESHOLD 1986 // 1.600 V (1.600/3.3*4096 = 1985.94)

#define LED_BLINK_DURATION 400ULL // ms
#define LED_BLINK_DURATION_CYCLES ((DEFAULT_SYSTEM_CLOCK/1000ULL) * LED_BLINK_DURATION)

#define RGB_BASE_DELAY 60ULL     // ms to wait before the FIRST LED starts
#define RGB_PULSE_DURATION 220ULL // Total ms for a single LED's pulse (100ms in, 100ms out)
#define RGB_PULSE_OFFSET 60ULL    // ms delay between each successive LED starting

#define RGB_BASE_DELAY_CYCLES ((DEFAULT_SYSTEM_CLOCK/1000ULL) * RGB_BASE_DELAY)
#define RGB_PULSE_DURATION_CYCLES ((DEFAULT_SYSTEM_CLOCK/1000ULL) * RGB_PULSE_DURATION)
#define RGB_PULSE_OFFSET_CYCLES ((DEFAULT_SYSTEM_CLOCK/1000ULL) * RGB_PULSE_OFFSET)

void bpm_led_task() {
	uint64_t elapsed_cycles = t - last_rising_edge_t;
	if (elapsed_cycles >= LED_BLINK_DURATION_CYCLES) { // duration has passed, turn led off
		yellow_led(0);
	} else {
		int64_t yellow_led_brightness = 255 - ((elapsed_cycles * 255) / LED_BLINK_DURATION_CYCLES);
		if (yellow_led_brightness > 255) {
			yellow_led(255);
		} else if (yellow_led_brightness < 0) {
			yellow_led(0);
		} else {
			yellow_led((uint8_t)yellow_led_brightness);
		}
	}
		
	// Loop through LED pairs 0, 1, and 2
	for (int i = 0; i < 3; i++) {
		// Calculate the specific start and end time for this specific pair of LEDs
		uint64_t led_start_cycles = RGB_BASE_DELAY_CYCLES + (i * RGB_PULSE_OFFSET_CYCLES);
		uint64_t led_end_cycles = led_start_cycles + RGB_PULSE_DURATION_CYCLES;
		
		// If this LED is outside its active window, force it to 0
		if (elapsed_cycles < led_start_cycles || elapsed_cycles >= led_end_cycles) {
				rgb_set_led(i, 0, 0, 0);
				rgb_set_led(i+3, 0, 0, 0);
		}
		
		// If this LED is inside its active window, calculate its triangle wave
		else {
			uint64_t pulse_elapsed = elapsed_cycles - led_start_cycles;
			uint64_t half_pulse = RGB_PULSE_DURATION_CYCLES / 2;
			int64_t led_brightness = 0;
			
			if (pulse_elapsed <= half_pulse) {
				// Fading IN (0 up to 255)
				led_brightness = (pulse_elapsed * 255) / half_pulse;
			} else {
				// Fading OUT (255 down to 0)
				led_brightness = 255 - (((pulse_elapsed - half_pulse) * 255) / half_pulse);
			}
			
			// Safety clamp and apply
			led_brightness = (led_brightness > 255) ? 255 : ((led_brightness < 0) ? 0 : led_brightness);
			rgb_set_led(i, (uint8_t)led_brightness*0.1, (uint8_t)led_brightness*0.03, 0);
			rgb_set_led(i+3, (uint8_t)led_brightness*0.1, (uint8_t)led_brightness*0.03, 0);
		}
	}
	rgb_refresh();
}

uint64_t credits_scroll_timer = 0;
unsigned int credits_scroll_index = 0;
char credits_scroll_text[9]; 

uint64_t test_mode_timer = 0;
unsigned int test_mode_index = 0;

void init_dac() {
	// Enable clock to DAC and port E
	SIM->SCGC6 |= SIM_SCGC6_DAC0_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTE_MASK;
	
	// Select analog for pin
	PORTE->PCR[DAC_PIN] &= ~PORT_PCR_MUX_MASK;
	PORTE->PCR[DAC_PIN] |= PORT_PCR_MUX(0);
	
	// Disable buffer mode
	DAC0->C1 = 0;
	DAC0->C2 = 0;
	
	// Enable DAC, seect VDDA as reference voltage
	DAC0->C0 = DAC_C0_DACEN_MASK | DAC_C0_DACRFS_MASK;
}

uint64_t credits_rainbow_timer = 0;
uint8_t credits_rainbow_hue = 0;

int main(void) {
	// Enable clocks
	SIM->SCGC5 |= SIM_SCGC5_PORTA_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTB_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTC_MASK;
	SIM->SCGC5 |= SIM_SCGC5_PORTD_MASK;
	
	SysTick_Config(10000); // Increment the counter every 10000 clock cycles

	
	LCD_init(); // initialize LCD display
	LED_init();
	buttons_init();
	
	init_adc();
	init_dac();
	
	spi0_init(); // Initiallize SPI for APA102 leds
	
	for (int i=0; i<8; i++) {
		LCD_create_char(i, STARTUP_LOGO[i]);
	}
	
	rgb_set_leds(0,0,0);
	rgb_refresh();
	
	
	
	LCD_home();
	for (int i=0; i<8; i++) {
		LCD_send_data(i); // Display the startup logo on the first 8 characters
	}
	LCD_set_cursor(0, 1);
	LCD_send_string("Starting");
	
	for (int i=0; i<=280; i++) {
		if (i <= 80) {
			red_led(i);
			rgb_set_led(0,i*2,0,0);
			rgb_set_led(3,i*2,0,0);
		} else if (i <= 160) {
			red_led(160-i);
			rgb_set_led(0,(160-i)*2,0,0);
			rgb_set_led(3,(160-i)*2,0,0);
		}
		if (i >= 60 && i <= 140) {
			yellow_led((i-60)*2);
			rgb_set_led(1,(i-60),(i-60)*0.3,0);
			rgb_set_led(4,(i-60),(i-60)*0.3,0);
		} else if (i >= 140 && i <= 220) {
			yellow_led((220-i)*2);
			rgb_set_led(1,(220-i),(220-i)*0.3,0);
			rgb_set_led(4,(220-i),(220-i)*0.3,0);
		}
		if (i >= 120 && i <= 200) {
			rgb_set_led(2,0,(i-120),0);
			rgb_set_led(5,0,(i-120),0);
		} else if (i >= 200 && i <= 280) {
			rgb_set_led(2,0,(280-i),0);
			rgb_set_led(5,0,(280-i),0);
		}
		if (i == 120) {
			green_led(1);
		}
		if (i == 280) {
			green_led(0);
		}
		rgb_refresh();
		delay_ms(5);
	}
	
	// Load left and right arrow custom characters
	LCD_create_char(0, LEFT_ARROW);
	LCD_create_char(1, RIGHT_ARROW);
	LCD_create_char(2, LEFT_ARROW_INV);
	LCD_create_char(3, RIGHT_ARROW_INV);
	
	LCD_clear();
	LCD_send_string("Measure");
	LCD_set_cursor(1, 1);
	LCD_send_data(0);
	LCD_set_cursor(6, 1);
	LCD_send_data(1);
	
	char lcd_render_buffer[9];
	
	while (1) {
		t = get_safe_time();
		button_task();
		
		if (screen < 5) { // We're selecting a menu option
			if (last_screen == 10) { // If we're coming from the measure mode, we need to make sure the yellow led is off.
				yellow_led(0);
				rgb_set_leds(0, 0, 0);
				rgb_refresh();
			}
			if (last_screen == 12) { // If we're coming from test mode, all the LCD characters will be blocks and we'll need to clear the LCD
				LCD_clear();
				red_led(0);
				yellow_led(0);
				green_led(0);
			}
			if (menu_option_slide != 0) { // If a slide has been triggered (the slide variable is not zero and is somewhere between -8 and 8)
				if (t - menu_option_slide_timer >= MENU_OPTION_SLIDE_TIME ) { // If it's time to slide
					menu_option_slide_timer = t;
					if (menu_option_slide < 0) { // Need to increment slide
						menu_option_slide++;
					} else { // Need to decrement slide
						menu_option_slide--;
					}
					get_slide_frame(screen, menu_option_slide, lcd_render_buffer); // Get the 8-character window of the menu options
					//LCD_clear();
					LCD_set_cursor(0, 0);
					LCD_send_string(lcd_render_buffer);
				}
			} else if (screen != last_screen) {
				yellow_led(0);
				rgb_set_leds(0, 0, 0);
				rgb_refresh();
				last_screen = screen;
				if (screen <= 4) {
					//LCD_clear();
					LCD_set_cursor(0, 0);
					LCD_send_string(menu_options_text[screen]);
					if (screen != 4) {
						LCD_set_cursor(6, 1);
						LCD_send_data(1);
					} else {
						LCD_set_cursor(6, 1);
						LCD_send_data(' ');
					}
					if (screen != 0) {
						LCD_set_cursor(1, 1);
						LCD_send_data(0);
					} else {
						LCD_set_cursor(1, 1);
						LCD_send_data(' ');
					}
				}
			}
		} else { // We're not selecting a menu option
			if (screen == 9) { // We're in credits mode
				if (screen != last_screen) { // Just got to this screen
					last_screen = screen;
					credits_scroll_index = 0;
					LCD_clear();
					LCD_set_cursor(0, 0);
					strncpy(credits_scroll_text, credits_text, 8); // Copy the first 8 characters from credits_text to the credits_scroll_text buffer
					LCD_send_string(credits_scroll_text); // Set to the first 8 characters of credits
					credits_scroll_timer = t;
				}
				if (t-credits_scroll_timer > CREDITS_SCROLL_TIME) { // Time for a scroll update!
					credits_scroll_timer = t;
					credits_scroll_index++;
					if (credits_scroll_index > CREDITS_TEXT_LEN) {
						credits_scroll_index = 0;
					}
					LCD_set_cursor(0, 0);
					strncpy(credits_scroll_text, credits_text+credits_scroll_index, 8); // Copy the first 8 characters from credits_text to the credits_scroll_text buffer
					LCD_send_string(credits_scroll_text); // Set to the first 8 characters of credits
				}
				
				// RGB LED Rainbow Stuff
				if (t - credits_rainbow_timer > 500000) { // The value here adjusts the speed of the rainbow animation
					credits_rainbow_timer = t;
					credits_rainbow_hue++; // Overflowing past 255 loops it back to 0 perfectly
					
					for (int i = 0; i < 6; i++) {
						// Spread the 6 LEDs evenly across the 256 color wheel (256 / 6 = ~42)
						uint8_t pixel_hue = credits_rainbow_hue + (i * 42);
						set_ring_rainbow_color(i, pixel_hue);
					}
					rgb_refresh();
				}
			}
			if (screen == 10) { // We're in measurement mode
				if (screen != last_screen) { // Just got to this screen
					last_screen = screen;
					LCD_clear();
					LCD_set_cursor(1, 0);
					LCD_send_string("-- BPM");
				}
				int value = adc_read(8); // Refresh current voltage sample
				
				// Output voltage sample to DAC
				DAC0->DAT[0].DATL = DAC_DATL_DATA0(value);
				DAC0->DAT[0].DATH = DAC_DATH_DATA1(value >> 8);
				
				if (value > UPPER_THRESHOLD && !adc_value_state) { // If we are above the upper threshold and were previously in the falling state
					// Inside here gets triggered once per rising edge
					period = t-last_rising_edge_t;
					period_ms = period/(DEFAULT_SYSTEM_CLOCK/1000);
					bpm = 60000/period_ms;
					last_rising_edge_t = t;
					adc_value_state = 1; // We acknowledge that the signal rose above the upper threshold so this only happens once
					if (period_ms < 10000) {
						bpm_active = 1;
					} else {
						bpm_active = 0;
					}
					if (t - lcd_update_timer  >= 10000) { // Limit the speed at which we update the LCD
						lcd_update_timer = t;
						if (bpm_active) {
							if (bpm < 10) {
								LCD_set_cursor(0, 0);
								LCD_send_data(' ');
								LCD_send_data(' ');
								LCD_send_number(bpm);
							}
							if (bpm < 100) {
								LCD_set_cursor(0, 0);
								LCD_send_data(' ');
								LCD_send_number(bpm);
							} else if (bpm < 1000) {
								LCD_set_cursor(0, 0);
								LCD_send_number(bpm);
							}
						} else {
							LCD_set_cursor(0, 0);
							LCD_send_string(" --");
						}
					}
				} else if (value < LOWER_THRESHOLD && adc_value_state) {
					adc_value_state = 0; // Acknowledge that the signal dropped below the lower threshold
				}
				bpm_led_task();
			}
			if (screen == 12) { // We're in test mode
				if (screen != last_screen) { // Just got to this screen
					last_screen = screen;
					LCD_clear();
					LCD_set_cursor(0, 0);
					// Fill the LCD screen with black blocks to test the pixels
					for (int i=0; i<8; i++) {
						LCD_send_data(255);
					}
					LCD_set_cursor(0, 1);
					for (int i=0; i<8; i++) {
						LCD_send_data(255);
					}
					test_mode_timer = 0;
					test_mode_index = 0;
				}
				if (t-test_mode_timer > TEST_MODE_TIME) { // Time for a test mode update!
					test_mode_timer = t;
					
					if (test_mode_index == 0) {
						rgb_set_leds(255,0,0);
						red_led(255);
						yellow_led(0);
						green_led(0);
					} else if (test_mode_index == 1) {
						rgb_set_leds(0,255,0);
						red_led(0);
						yellow_led(255);
						green_led(0);
					} else if (test_mode_index == 2) {
						rgb_set_leds(0,0,255);
						red_led(0);
						yellow_led(0);
						green_led(1);
					} else if (test_mode_index == 3) {
						rgb_set_leds(255,255,255);
						red_led(255);
						yellow_led(255);
						green_led(1);
					}
					rgb_refresh();
					test_mode_index++;
					if (test_mode_index == 4) {
						test_mode_index = 0;
					}
					
				}
			}
		}
	}
}


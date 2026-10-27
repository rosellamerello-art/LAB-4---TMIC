 
#define F_CPU 16000000UL	
#define PCF8574	0x27							

#include <avr/io.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#include "UART.h"
#include "SPI.h"
#include "RC522.h"
#include "i2c.h"
#include "lcd_i2c.h"

#define BAUD 9600
#define MY_UBRR F_CPU/16/BAUD-1
#define MAX_USUARIOS 3

#define LED_VERDE_PIN   PD7
#define LED_VERDE_PORT  PORTD
#define LED_VERDE_DDR   DDRD

#define LED_ROJO_PIN    PB0
#define LED_ROJO_PORT   PORTB
#define LED_ROJO_DDR    DDRB

#define BUZZER_PIN      PD6
#define BUZZER_PORT     PORTD
#define BUZZER_DDR      DDRD

#define EEPROM_MAGIC_ADDR 500  
#define EEPROM_MAGIC_VAL  0x42

#define EEPROM_USERS_OFFSET 1

#define KEYPAD_PORT_R	PORTD
#define KEYPAD_DDR_R	DDRD
#define KEYPAD_MASK_R	0X3C
#define KEYPAD_SHIFT_R  2

#define KEYPAD_PORT_C	PORTC
#define KEYPAD_PIN_C	PINC
#define KEYPAD_DDR_C	DDRC
#define KEYPAD_MASK_C	0X0F

const char keys[4][4] = {
	{'1', '2', '3', 'A'},
	{'4', '5', '6', 'B'},
	{'7', '8', '9', 'C'},
	{'*', '0', '#', 'D'}
};

void keypad_init(void) {
	KEYPAD_DDR_R |= KEYPAD_MASK_R;
	KEYPAD_PORT_R |= KEYPAD_MASK_R;
	
	KEYPAD_DDR_C &= ~KEYPAD_MASK_C;
	KEYPAD_PORT_C |= KEYPAD_MASK_C;
}

void indicadores_init(void) {
	LED_VERDE_DDR |= (1 << LED_VERDE_PIN);
	LED_ROJO_DDR  |= (1 << LED_ROJO_PIN);
	BUZZER_DDR    |= (1 << BUZZER_PIN);
	
	LED_VERDE_PORT &= ~(1 << LED_VERDE_PIN);
	LED_ROJO_PORT  &= ~(1 << LED_ROJO_PIN);
	BUZZER_PORT    &= ~(1 << BUZZER_PIN);
}

void encender_verde_breve(void) {
	LED_VERDE_PORT |= (1 << LED_VERDE_PIN);
	_delay_ms(500);
	LED_VERDE_PORT &= ~(1 << LED_VERDE_PIN);
}

void encender_rojo_breve(void) {
	LED_ROJO_PORT |= (1 << LED_ROJO_PIN);
	_delay_ms(500);
	LED_ROJO_PORT &= ~(1 << LED_ROJO_PIN);
}

void sonar_buzzer_alarma(void) {
	for (uint8_t i = 0; i < 3; i++) {
		BUZZER_PORT |= (1 << BUZZER_PIN);
		LED_ROJO_PORT |= (1 << LED_ROJO_PIN);
		_delay_ms(300);
		BUZZER_PORT &= ~(1 << BUZZER_PIN);
		LED_ROJO_PORT &= ~(1 << LED_ROJO_PIN);
		_delay_ms(300);
	}
}

char keypad_get_key(void) {
	for (uint8_t r = 0; r < 4; r++) {
		KEYPAD_PORT_R |= KEYPAD_MASK_R;
		KEYPAD_PORT_R &= ~(1 << (r + KEYPAD_SHIFT_R));
		_delay_ms(5);
		
		for (uint8_t c = 0; c < 4; c++) {
			if (!(KEYPAD_PIN_C & (1 << c))) { 
				while (!(KEYPAD_PIN_C & (1 << c)));
				KEYPAD_PORT_R |= KEYPAD_MASK_R;
				return keys[r][c];
			}
		}
	}
	KEYPAD_PORT_R |= KEYPAD_MASK_R;
	return '\0';
}

char keypad_wait_key(void) {
	char key = '\0';
	while (key == '\0') {
		key = keypad_get_key();
	}
	return key;
}

typedef struct {
	uint8_t uid[4];
	uint16_t pin;
	uint32_t saldo;
} EEMEM Usuario_t;

typedef enum {
	ESTADO_INICIO,
	ESTADO_ESPERANDO_TARJETA,
	ESTADO_CORROBORANDO_PIN,
	ESTADO_ELEGIR_OPERACION,
	ESTADO_CONSULTAR_SALDO,
	ESTADO_REALIZAR_DEPOSITO,
	ESTADO_REALIZAR_RETIRO,
} Estado_t;

void eeprom_inicializar_datos_prueba(void) {
	uint8_t magic = eeprom_read_byte((uint8_t*)EEPROM_MAGIC_ADDR);
	
	if (magic == EEPROM_MAGIC_VAL) {
		return;
	}
	
	Usuario_t usuarios_inicales[MAX_USUARIOS] = {
		{ .uid = {0xA1, 0xB2, 0xC3, 0xD4}, .pin = 1234, .saldo = 5000 },
		{ .uid = {0x12, 0x34, 0x56, 0x78}, .pin = 5678, .saldo = 10000 },
		{ .uid = {0x11, 0x22, 0x33, 0x44}, .pin = 4321, .saldo = 2500 }
	};
	
	for (uint8_t i = 0; i < MAX_USUARIOS; i++) {
		eeprom_update_block(&usuarios_inicales[i], (void*)(i * sizeof(Usuario_t)), sizeof(Usuario_t));
	}
	
	eeprom_update_byte((uint8_t*)EEPROM_MAGIC_ADDR, EEPROM_MAGIC_VAL);
}
		
int8_t eeprom_buscar_tarjeta(uint8_t *uid_leido) {
	Usuario_t temp_user;
	for (uint8_t i = 0; i < MAX_USUARIOS; i++) {
		eeprom_read_block(&temp_user, (void*)(i * sizeof(Usuario_t)), sizeof(Usuario_t));
		if (memcmp(temp_user.uid, uid_leido, 4) == 0) {
			return i;
		}
	}
	return -1;
}

void eeprom_actualizar_saldo(uint8_t indice, uint32_t nuevo_saldo) {
	uint16_t direccion_saldo = (indice * sizeof(Usuario_t)) + __builtin_offsetof(Usuario_t, saldo);
	eeprom_update_dword((uint32_t*)direccion_saldo, nuevo_saldo);
}

int main(void) {
	Estado_t estado_actual = ESTADO_INICIO;
	char card_uid[10] = {0};
	char pin_buffer[10] = {0};
	uint8_t index = 0;
	uint8_t intentos_fallidos_pin = 0; 
	Usuario_t usuario_actual;
	int8_t indice_usuario = -1;
	
	while (1) {
		switch (estado_actual) {
			
			case ESTADO_INICIO:
				uart_init(MY_UBRR);
				spi_init();
				mfrc522_resetPinInit();
				mfrc522_init();
				keypad_init();
				indicadores_init();
				
				LCD_Init();
				LCD_Clear();
				LCD_SetCursor(0, 0);
				LCD_String("Sistema Iniciado");
				
				intentos_fallidos_pin = 0;
				_delay_ms(1000);
				
				eeprom_inicializar_datos_prueba();
				estado_actual = ESTADO_ESPERANDO_TARJETA;
				break;
				
			case ESTADO_ESPERANDO_TARJETA:
				LCD_Clear();
				LCD_SetCursor(0, 0);
				LCD_String("Acerque tarjeta");
				LCD_SetCursor(1, 0);
				LCD_String("por favor...");
				
				memset(card_uid, 0, 10);
				mfrc522_standard(card_uid);
				_delay_ms(100);
				
				if(card_uid[0] != 0x00 && card_uid[0] != 0xFF) {
					
					char uart_msg[50];
					sprintf(uart_msg, "Tarjeta detectada! UID: %02X %02X %02X %02X\r\n", 
							card_uid[0], card_uid[1], card_uid[2], card_uid[3]);
					uart_print(uart_msg);
					
					indice_usuario = eeprom_buscar_tarjeta(card_uid);
					
					if (indice_usuario >= 0) {
						eeprom_read_block(&usuario_actual, (void*)(indice_usuario * sizeof(Usuario_t)), sizeof(Usuario_t));
						
						encender_verde_breve();
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("Tarjeta Valida");
						_delay_ms(1000);
						
						estado_actual = ESTADO_CORROBORANDO_PIN;
					} else {
						encender_rojo_breve();
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("No Registrada");
						_delay_ms(1500);
					}
				}
				break;
				
			case ESTADO_CORROBORANDO_PIN:
				index = 0;
				LCD_Clear();
				LCD_SetCursor(0, 0);
				LCD_String("Ingrese PIN:");
				LCD_SetCursor(1, 0);
				
				while (1) {
					char c = keypad_wait_key();
					if (c != 0) {
						if (c == '#') {
							pin_buffer[index] = '\0';
							break;
						} else if (index < 4 && c >= '0' && c <= '9') {
							pin_buffer[index++] = c;
							LCD_Char('*');
						}
					}
				}
				uint16_t pin_ingresado = (uint16_t)atoi(pin_buffer);
				
				if (pin_ingresado == usuario_actual.pin) {
					encender_verde_breve();
					LCD_Clear();
					LCD_SetCursor(0, 0);
					LCD_String("PIN Correcto");
					_delay_ms(1000);
					
					intentos_fallidos_pin = 0; 
					estado_actual = ESTADO_ELEGIR_OPERACION;
				} else {
					intentos_fallidos_pin++;
					
					if (intentos_fallidos_pin >= 3) {
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("Bloqueado x 3 fallos");
						sonar_buzzer_alarma();
						
						intentos_fallidos_pin = 0;
						estado_actual = ESTADO_INICIO;
					} else {
						encender_rojo_breve();
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("PIN Incorrecto");
						LCD_SetCursor(1, 0);
						char msg_intentos[20];
						sprintf(msg_intentos, "Intentos: %d/3", intentos_fallidos_pin);
						LCD_String(msg_intentos);
						_delay_ms(2000);
						
						estado_actual = ESTADO_CORROBORANDO_PIN;
					}
				}
				break;
				
			case ESTADO_ELEGIR_OPERACION:
				LCD_Clear();
				LCD_SetCursor(0, 0);
				LCD_String("A:Saldo B:Dep");
				LCD_SetCursor(1, 0);
				LCD_String("C:Ret D:Salir");
				
				while (1) {
					char c = keypad_wait_key();
					if (c != 0) {
						if (c == 'A' || c == 'a') {
							estado_actual = ESTADO_CONSULTAR_SALDO;
							break;
						} else if (c == 'B' || c == 'b') {
							estado_actual = ESTADO_REALIZAR_DEPOSITO;
							break;
						} else if (c == 'C' || c == 'c') {
							estado_actual = ESTADO_REALIZAR_RETIRO;
							break;
						} else if (c == 'D' || c == 'd') {
							encender_verde_breve();
							intentos_fallidos_pin = 0;
							estado_actual = ESTADO_ESPERANDO_TARJETA;
							break;
						}
					}
				}
				break;
				
			case ESTADO_CONSULTAR_SALDO:
				{
					char buffer_saldo[30];
					sprintf(buffer_saldo, "Saldo: $%lu", usuario_actual.saldo);
					
					encender_verde_breve();
					LCD_Clear();
					LCD_SetCursor(0, 0);
					LCD_String("Su Saldo Actual:");
					LCD_SetCursor(1, 0);
					LCD_String(buffer_saldo);
				}
				_delay_ms(2500);
				estado_actual = ESTADO_ELEGIR_OPERACION;
				break;
				
			case ESTADO_REALIZAR_DEPOSITO:
				{
					char monto_buffer[12] = {0};
					uint8_t m_index = 0;
					
					LCD_Clear();
					LCD_SetCursor(0, 0);
					LCD_String("Monto Deposito:");
					LCD_SetCursor(1, 0);
					
					while (1) {
						char c = keypad_wait_key();
						if (c != 0) {
							if (c == '#') {
								monto_buffer[m_index] = '\0';
								break;
							} else if (m_index < 10 && c >= '0' && c <= '9') {
								monto_buffer[m_index++] = c;
								LCD_Char(c);
							}
						}
					}
					uint32_t deposito = (uint32_t)atol(monto_buffer);
					if (deposito > 0) {
						usuario_actual.saldo += deposito;
						eeprom_actualizar_saldo(indice_usuario, usuario_actual.saldo);
						
						encender_verde_breve();
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("Dep. Exitoso!");
					} else {
						encender_rojo_breve();
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("Monto Invalido");
					}
				}
				_delay_ms(2000);
				estado_actual = ESTADO_ELEGIR_OPERACION;
				break;
				
			case ESTADO_REALIZAR_RETIRO:
				{
					char monto_buffer[12] = {0};
					uint8_t m_index = 0;
					
					LCD_Clear();
					LCD_SetCursor(0, 0);
					LCD_String("Monto Retiro:");
					LCD_SetCursor(1, 0);
					
					while (1) {
						char c = keypad_wait_key();
						if (c != 0) {
							if (c == '#') {
								monto_buffer[m_index] = '\0';
								break;
							} else if (m_index < 10 && c >= '0' && c <= '9') {
								monto_buffer[m_index++] = c;
								LCD_Char(c);
							}
						}
					}
					uint32_t retiro = (uint32_t)atol(monto_buffer);
					if (retiro > 0 && retiro <= usuario_actual.saldo) {
						usuario_actual.saldo -= retiro;
						eeprom_actualizar_saldo(indice_usuario, usuario_actual.saldo);
						
						encender_verde_breve();
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("Retiro Exitoso!");
					} else {
						encender_rojo_breve();
						LCD_Clear();
						LCD_SetCursor(0, 0);
						LCD_String("Saldo Insuf./Inv.");
					}
				}
				_delay_ms(2000);
				estado_actual = ESTADO_ELEGIR_OPERACION;
				break;
		}	
	}
}

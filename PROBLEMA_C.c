#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/power.h>
#include <avr/interrupt.h>
#include <util/delay.h>
#include <stdint.h>

#define UMBRAL_LDR 500


// ======================================================
// UART
// ======================================================

void UART_Init(void)
{
	UCSR0A = 0;

	// 9600 baudios a 16 MHz
	UBRR0H = 0;
	UBRR0L = 103;

	UCSR0B = (1 << RXEN0) |
	(1 << TXEN0);

	UCSR0C = (1 << UCSZ01) |
	(1 << UCSZ00);
}


void UART_Enviar_Char(char dato)
{
	while (!(UCSR0A & (1 << UDRE0)));

	UDR0 = dato;
}


void UART_Enviar_Texto(const char *texto)
{
	while (*texto)
	{
		UART_Enviar_Char(*texto++);
	}
}


uint8_t UART_Hay_Dato(void)
{
	return (UCSR0A & (1 << RXC0));
}


char UART_Leer(void)
{
	return UDR0;
}


// ======================================================
// ADC - LDR
// ======================================================

void ADC_Init(void)
{
	// Referencia AVCC
	ADMUX = (1 << REFS0);

	// ADC habilitado - Prescaler 128
	ADCSRA = (1 << ADEN) |
	(1 << ADPS2) |
	(1 << ADPS1) |
	(1 << ADPS0);
}


uint16_t ADC_Leer(uint8_t canal)
{
	ADMUX = (ADMUX & 0xF0) |
	(canal & 0x0F);

	_delay_us(10);

	ADCSRA |= (1 << ADSC);

	while (ADCSRA & (1 << ADSC));

	return ADC;
}


// ======================================================
// BUZZER ACTIVO
// PD6 -> R 1k -> BASE 2N2222
// ======================================================

void Buzzer_Init(void)
{
	DDRD |= (1 << PD6);

	PORTD &= ~(1 << PD6);
}


void Buzzer_ON(void)
{
	PORTD |= (1 << PD6);
}


void Buzzer_OFF(void)
{
	PORTD &= ~(1 << PD6);
}


// ======================================================
// ACTIVAR ALARMA
// ======================================================

void Activar_Alarma(const char *zona)
{
	// LED verde OFF
	PORTD &= ~(1 << PD4);

	// LED rojo ON
	PORTD |= (1 << PD5);

	// Buzzer ON
	Buzzer_ON();

	UART_Enviar_Texto(
	"\r\nALARMA:ON\r\n"
	);

	UART_Enviar_Texto(
	"ZONA:"
	);

	UART_Enviar_Texto(zona);

	UART_Enviar_Texto("\r\n");
}


// ======================================================
// MOSTRAR ESTADO
// ======================================================

void Mostrar_Estado(uint8_t armado,
uint8_t modo_auto,
uint8_t alarma)
{
	UART_Enviar_Texto(
	"\r\n--- ESTADO DEL SISTEMA ---\r\n"
	);


	// ARMADO / DESARMADO
	if (armado)
	{
		UART_Enviar_Texto(
		"ESTADO:ARMADO\r\n"
		);
	}
	else
	{
		UART_Enviar_Texto(
		"ESTADO:DESARMADO\r\n"
		);
	}


	// AUTO / MANUAL
	if (modo_auto)
	{
		UART_Enviar_Texto(
		"MODO:AUTO\r\n"
		);
	}
	else
	{
		UART_Enviar_Texto(
		"MODO:MANUAL\r\n"
		);
	}


	// ALARMA
	if (alarma)
	{
		UART_Enviar_Texto(
		"ALARMA:ON\r\n"
		);
	}
	else
	{
		UART_Enviar_Texto(
		"ALARMA:OFF\r\n"
		);
	}


	// ESTADO DE LA SIRENA
	if (PORTD & (1 << PD6))
	{
		UART_Enviar_Texto(
		"SIRENA:ON\r\n"
		);
	}
	else
	{
		UART_Enviar_Texto(
		"SIRENA:OFF\r\n"
		);
	}


	UART_Enviar_Texto(
	"--------------------------\r\n"
	);
}


// ======================================================
// MAIN
// ======================================================

int main(void)
{
	uint8_t armado = 0;
	uint8_t alarma = 0;

	// 1 = AUTO
	// 0 = MANUAL
	uint8_t modo_auto = 1;

	uint16_t adc_ldr;

	char comando;


	// ==================================================
	// RELOJ SIN DIVISION
	// ==================================================

	cli();

	clock_prescale_set(clock_div_1);

	sei();


	// ==================================================
	// CONFIGURACION DE PINES
	// ==================================================

	// PD4 = LED VERDE
	DDRD |= (1 << PD4);

	// PD5 = LED ROJO
	DDRD |= (1 << PD5);

	// PD6 = BUZZER
	DDRD |= (1 << PD6);

	// PD2 = SENSOR PRESENCIA
	DDRD &= ~(1 << PD2);

	// Pull-up interno PD2
	PORTD |= (1 << PD2);


	// LEDs apagados
	PORTD &= ~(1 << PD4);
	PORTD &= ~(1 << PD5);

	// Buzzer apagado
	PORTD &= ~(1 << PD6);


	// ==================================================
	// INICIALIZACIONES
	// ==================================================

	UART_Init();

	ADC_Init();

	Buzzer_Init();

	_delay_ms(500);


	// ==================================================
	// MENU INICIAL
	// ==================================================

	UART_Enviar_Texto(
	"\r\nSISTEMA DE ALARMA\r\n"
	);

	UART_Enviar_Texto(
	"A = ARMAR\r\n"
	);

	UART_Enviar_Texto(
	"D = DESARMAR\r\n"
	);

	UART_Enviar_Texto(
	"S = SILENCIAR\r\n"
	);

	UART_Enviar_Texto(
	"U = MODO AUTO\r\n"
	);

	UART_Enviar_Texto(
	"M = MODO MANUAL\r\n"
	);

	UART_Enviar_Texto(
	"F = FORZAR ALARMA\r\n"
	);

	UART_Enviar_Texto(
	"E = ESTADO\r\n"
	);

	UART_Enviar_Texto(
	"\r\nMODO INICIAL:AUTO\r\n"
	);


	// ==================================================
	// BUCLE PRINCIPAL
	// ==================================================

	while (1)
	{
		// ==============================================
		// COMANDOS UART
		// ==============================================

		if (UART_Hay_Dato())
		{
			comando = UART_Leer();


			// ==========================================
			// A = ARMAR
			// ==========================================

			if ((comando == 'A') ||
			(comando == 'a'))
			{
				armado = 1;
				alarma = 0;

				PORTD |= (1 << PD4);

				PORTD &= ~(1 << PD5);

				Buzzer_OFF();

				UART_Enviar_Texto(
				"\r\nSISTEMA:ARMADO\r\n"
				);
			}


			// ==========================================
			// D = DESARMAR
			// ==========================================

			else if ((comando == 'D') ||
			(comando == 'd'))
			{
				armado = 0;
				alarma = 0;

				PORTD &= ~(1 << PD4);

				PORTD &= ~(1 << PD5);

				Buzzer_OFF();

				UART_Enviar_Texto(
				"\r\nSISTEMA:DESARMADO\r\n"
				);
			}


			// ==========================================
			// S = SILENCIAR
			// ==========================================

			else if ((comando == 'S') ||
			(comando == 's'))
			{
				if (alarma)
				{
					Buzzer_OFF();

					UART_Enviar_Texto(
					"\r\nALARMA:SILENCIADA\r\n"
					);
				}
				else
				{
					UART_Enviar_Texto(
					"\r\nNO HAY ALARMA ACTIVA\r\n"
					);
				}
			}


			// ==========================================
			// U = MODO AUTO
			// ==========================================

			else if ((comando == 'U') ||
			(comando == 'u'))
			{
				modo_auto = 1;

				UART_Enviar_Texto(
				"\r\nMODO:AUTO\r\n"
				);
			}


			// ==========================================
			// M = MODO MANUAL
			// ==========================================

			else if ((comando == 'M') ||
			(comando == 'm'))
			{
				modo_auto = 0;

				UART_Enviar_Texto(
				"\r\nMODO:MANUAL\r\n"
				);
			}


			// ==========================================
			// F = FORZAR ALARMA
			// ==========================================

			else if ((comando == 'F') ||
			(comando == 'f'))
			{
				if (!modo_auto)
				{
					if (armado)
					{
						alarma = 1;

						Activar_Alarma(
						"REMOTA"
						);
					}
					else
					{
						UART_Enviar_Texto(
						"\r\nSISTEMA DESARMADO\r\n"
						);
					}
				}
				else
				{
					UART_Enviar_Texto(
					"\r\nCOMANDO SOLO EN MANUAL\r\n"
					);
				}
			}


			// ==========================================
			// E = CONSULTAR ESTADO
			// ==========================================

			else if ((comando == 'E') ||
			(comando == 'e'))
			{
				Mostrar_Estado(
				armado,
				modo_auto,
				alarma
				);
			}
		}


		// ==============================================
		// LEER LDR
		// ==============================================

		adc_ldr = ADC_Leer(0);


		// ==============================================
		// MODO AUTOMATICO
		// ==============================================

		if (modo_auto &&
		armado &&
		!alarma)
		{
			// ==========================================
			// LDR - APERTURA
			// ==========================================

			if (adc_ldr > UMBRAL_LDR)
			{
				alarma = 1;

				Activar_Alarma(
				"VENTANA"
				);
			}


			// ==========================================
			// PRESENCIA - PD2
			// ==========================================

			else if (!(PIND & (1 << PD2)))
			{
				_delay_ms(30);

				if (!(PIND & (1 << PD2)))
				{
					alarma = 1;

					Activar_Alarma(
					"MOVIMIENTO"
					);
				}
			}
		}


		_delay_ms(50);
	}
}

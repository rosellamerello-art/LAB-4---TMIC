#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <stdint.h>
#include <math.h>

#define MPU6050_ADDR      0x68
#define REG_PWR_MGMT_1    0x6B
#define REG_ACCEL_XOUT    0x3B

#define RAD_A_GRADOS      57.2957795

// ======================================================
// EEPROM
// ======================================================

#define EEPROM_MAGIC 0xA5

int16_t EEMEM ee_offsetRoll;
int16_t EEMEM ee_offsetPitch;
uint8_t EEMEM ee_magic;


// ======================================================
// UART
// ======================================================

void UART_Init(void)
{
	UBRR0H = 0;
	UBRR0L = 103;

	UCSR0B = (1 << TXEN0);

	UCSR0C =
	(1 << UCSZ01) |
	(1 << UCSZ00);
}

void UART_Char(char c)
{
	while (!(UCSR0A & (1 << UDRE0)));

	UDR0 = c;
}

void UART_Texto(const char *texto)
{
	while (*texto)
	{
		UART_Char(*texto++);
	}
}

void UART_Numero(int16_t numero)
{
	char buffer[8];
	uint8_t i = 0;

	if (numero == 0)
	{
		UART_Char('0');
		return;
	}

	if (numero < 0)
	{
		UART_Char('-');
		numero = -numero;
	}

	while (numero > 0)
	{
		buffer[i++] = (numero % 10) + '0';
		numero /= 10;
	}

	while (i > 0)
	{
		UART_Char(buffer[--i]);
	}
}


// ======================================================
// VALOR ABSOLUTO
// ======================================================

int16_t Absoluto(int16_t valor)
{
	if (valor < 0)
	{
		return -valor;
	}

	return valor;
}


// ======================================================
// I2C
// ======================================================

void I2C_Init(void)
{
	TWSR = 0x00;
	TWBR = 72;

	TWCR = (1 << TWEN);
}

void I2C_Start(void)
{
	TWCR =
	(1 << TWINT) |
	(1 << TWSTA) |
	(1 << TWEN);

	while (!(TWCR & (1 << TWINT)));
}

void I2C_Stop(void)
{
	TWCR =
	(1 << TWINT) |
	(1 << TWSTO) |
	(1 << TWEN);
}

void I2C_Write(uint8_t dato)
{
	TWDR = dato;

	TWCR =
	(1 << TWINT) |
	(1 << TWEN);

	while (!(TWCR & (1 << TWINT)));
}

uint8_t I2C_Read_ACK(void)
{
	TWCR =
	(1 << TWINT) |
	(1 << TWEN) |
	(1 << TWEA);

	while (!(TWCR & (1 << TWINT)));

	return TWDR;
}

uint8_t I2C_Read_NACK(void)
{
	TWCR =
	(1 << TWINT) |
	(1 << TWEN);

	while (!(TWCR & (1 << TWINT)));

	return TWDR;
}


// ======================================================
// MPU6050
// ======================================================

void MPU_Write(uint8_t registro, uint8_t dato)
{
	I2C_Start();

	I2C_Write(MPU6050_ADDR << 1);
	I2C_Write(registro);
	I2C_Write(dato);

	I2C_Stop();
}

void MPU_Leer(
int16_t *ax,
int16_t *ay,
int16_t *az)
{
	uint8_t xh, xl;
	uint8_t yh, yl;
	uint8_t zh, zl;

	I2C_Start();

	I2C_Write(MPU6050_ADDR << 1);
	I2C_Write(REG_ACCEL_XOUT);

	I2C_Start();

	I2C_Write(
	(MPU6050_ADDR << 1) | 1
	);

	xh = I2C_Read_ACK();
	xl = I2C_Read_ACK();

	yh = I2C_Read_ACK();
	yl = I2C_Read_ACK();

	zh = I2C_Read_ACK();
	zl = I2C_Read_NACK();

	I2C_Stop();

	*ax =
	((int16_t)xh << 8) | xl;

	*ay =
	((int16_t)yh << 8) | yl;

	*az =
	((int16_t)zh << 8) | zl;
}


// ======================================================
// WS2812B - PD6
// ======================================================

static void WS2812_Byte(uint8_t dato)
{
	uint8_t contador = 8;

	asm volatile (

	"1:                         \n\t"

	"sbi %[puerto], %[pin]      \n\t"

	"sbrc %[dato], 7            \n\t"
	"rjmp 2f                    \n\t"

	"cbi %[puerto], %[pin]      \n\t"

	"nop                        \n\t"
	"nop                        \n\t"
	"nop                        \n\t"
	"nop                        \n\t"
	"nop                        \n\t"

	"rjmp 3f                    \n\t"

	"2:                         \n\t"

	"nop                        \n\t"
	"nop                        \n\t"
	"nop                        \n\t"
	"nop                        \n\t"
	"nop                        \n\t"

	"cbi %[puerto], %[pin]      \n\t"

	"nop                        \n\t"
	"nop                        \n\t"

	"3:                         \n\t"

	"lsl %[dato]                \n\t"

	"dec %[contador]            \n\t"

	"brne 1b                    \n\t"

	: [dato] "+r" (dato),
	[contador] "+r" (contador)

	: [puerto] "I" (_SFR_IO_ADDR(PORTD)),
	[pin] "I" (PD6)
	);
}

void WS2812_Pixel(
uint8_t rojo,
uint8_t verde,
uint8_t azul)
{
	WS2812_Byte(verde);
	WS2812_Byte(rojo);
	WS2812_Byte(azul);
}

void WS2812_Reset(void)
{
	PORTD &= ~(1 << PD6);

	_delay_us(100);
}


// ======================================================
// MOSTRAR PIXEL
// ======================================================

void MostrarPixel(
uint8_t fila,
uint8_t columna,
uint8_t rojo,
uint8_t verde,
uint8_t azul)
{
	uint8_t i;

	uint8_t posicion =
	fila * 8 + columna;

	cli();

	for (i = 0; i < 64; i++)
	{
		if (i == posicion)
		{
			WS2812_Pixel(
			rojo,
			verde,
			azul
			);
		}
		else
		{
			WS2812_Pixel(
			0,
			0,
			0
			);
		}
	}

	sei();

	WS2812_Reset();
}


// ======================================================
// APAGAR MATRIZ
// ======================================================

void ApagarMatriz(void)
{
	uint8_t i;

	cli();

	for (i = 0; i < 64; i++)
	{
		WS2812_Pixel(
		0,
		0,
		0
		);
	}

	sei();

	WS2812_Reset();
}


// ======================================================
// MAPEO ±30° -> 0...7
// ======================================================

uint8_t Mapear(int16_t angulo)
{
	if (angulo < -30)
	{
		angulo = -30;
	}

	if (angulo > 30)
	{
		angulo = 30;
	}

	return (uint8_t)(
	((int32_t)(angulo + 30) * 7) / 60
	);
}


// ======================================================
// CALIBRAR - PD4
// ======================================================

uint8_t BotonCalibrar(void)
{
	if (!(PIND & (1 << PD4)))
	{
		_delay_ms(30);

		if (!(PIND & (1 << PD4)))
		{
			while (!(PIND & (1 << PD4)))
			{
			}

			_delay_ms(30);

			return 1;
		}
	}

	return 0;
}


// ======================================================
// HOLD - PD2
// ======================================================

uint8_t BotonHold(void)
{
	if (!(PIND & (1 << PD2)))
	{
		_delay_ms(30);

		if (!(PIND & (1 << PD2)))
		{
			while (!(PIND & (1 << PD2)))
			{
			}

			_delay_ms(30);

			return 1;
		}
	}

	return 0;
}


// ======================================================
// MAIN
// ======================================================

int main(void)
{
	int16_t ax;
	int16_t ay;
	int16_t az;

	int16_t rollMedido = 0;
	int16_t pitchMedido = 0;

	int16_t rollFiltrado = 0;
	int16_t pitchFiltrado = 0;

	int16_t roll = 0;
	int16_t pitch = 0;

	int16_t offsetRoll = 0;
	int16_t offsetPitch = 0;

	int16_t absRoll;
	int16_t absPitch;


	// ==================================================
	// FILTRO 4 MUESTRAS
	// ==================================================

	int16_t r1 = 0;
	int16_t r2 = 0;
	int16_t r3 = 0;
	int16_t r4 = 0;

	int16_t p1 = 0;
	int16_t p2 = 0;
	int16_t p3 = 0;
	int16_t p4 = 0;

	uint8_t filtroInicializado = 0;


	// ==================================================
	// MATRIZ
	// ==================================================

	uint8_t fila = 3;
	uint8_t columna = 3;

	uint8_t rojo = 0;
	uint8_t verde = 0;
	uint8_t azul = 100;


	// ==================================================
	// ESTADOS
	// ==================================================

	uint8_t hold = 0;

	uint8_t enBorde = 0;
	uint8_t enBordeAnterior = 0;

	uint8_t alarmaHabilitada = 0;

	// Terminal lento
	uint8_t contadorTerminal = 0;


	// ==================================================
	// RELOJ
	// ==================================================

	cli();

	clock_prescale_set(
	clock_div_1
	);

	sei();


	// ==================================================
	// PINES
	// ==================================================

	// WS2812
	DDRD |= (1 << PD6);
	PORTD &= ~(1 << PD6);

	// Buzzer
	DDRD |= (1 << PD3);
	PORTD &= ~(1 << PD3);

	// HOLD
	DDRD &= ~(1 << PD2);
	PORTD |= (1 << PD2);

	// CALIBRAR
	DDRD &= ~(1 << PD4);
	PORTD |= (1 << PD4);


	// ==================================================
	// UART + I2C
	// ==================================================

	UART_Init();

	I2C_Init();

	_delay_ms(100);


	// ==================================================
	// DESPERTAR MPU6050
	// ==================================================

	MPU_Write(
	REG_PWR_MGMT_1,
	0x00
	);

	_delay_ms(100);


	UART_Texto(
	"\r\nPROBLEMA E - VERSION FINAL\r\n"
	);

	UART_Texto(
	"FILTRO + HOLD + CALIBRAR + EEPROM + LIMITE\r\n"
	);


	// ==================================================
	// LEER CALIBRACION GUARDADA
	// ==================================================

	if (
	eeprom_read_byte(
	&ee_magic
	) == EEPROM_MAGIC
	)
	{
		offsetRoll =
		eeprom_read_word(
		(uint16_t *)&ee_offsetRoll
		);

		offsetPitch =
		eeprom_read_word(
		(uint16_t *)&ee_offsetPitch
		);


		// Protección de EEPROM
		if (
		offsetRoll < -180 ||
		offsetRoll > 180 ||
		offsetPitch < -180 ||
		offsetPitch > 180
		)
		{
			offsetRoll = 0;
			offsetPitch = 0;

			UART_Texto(
			"EEPROM INVALIDA - OFFSET=0\r\n"
			);
		}
		else
		{
			UART_Texto(
			"EEPROM RECUPERADA R="
			);

			UART_Numero(
			offsetRoll
			);

			UART_Texto(
			" P="
			);

			UART_Numero(
			offsetPitch
			);

			UART_Texto(
			"\r\n"
			);
		}
	}
	else
	{
		offsetRoll = 0;
		offsetPitch = 0;

		UART_Texto(
		"SIN CALIBRACION EEPROM\r\n"
		);
	}


	// ==================================================
	// LOOP PRINCIPAL
	// ==================================================

	while (1)
	{
		// ==============================================
		// HOLD
		// ==============================================

		if (BotonHold())
		{
			hold = !hold;

			if (hold)
			{
				UART_Texto(
				"HOLD=ON\r\n"
				);
			}
			else
			{
				UART_Texto(
				"HOLD=OFF\r\n"
				);
			}
		}


		// ==============================================
		// SENSOR
		// ==============================================

		if (!hold)
		{
			MPU_Leer(
			&ax,
			&ay,
			&az
			);


			// ------------------------------------------
			// ROLL
			// ------------------------------------------

			rollMedido =
			(int16_t)(
			atan2(
			(float)ay,
			(float)az
			)
			* RAD_A_GRADOS
			);


			// ------------------------------------------
			// PITCH
			// ------------------------------------------

			pitchMedido =
			(int16_t)(
			atan2(
			-(float)ax,
			sqrt(
			((float)ay * (float)ay) +
			((float)az * (float)az)
			)
			)
			* RAD_A_GRADOS
			);


			// ==========================================
			// FILTRO
			// ==========================================

			if (!filtroInicializado)
			{
				r1 = rollMedido;
				r2 = rollMedido;
				r3 = rollMedido;
				r4 = rollMedido;

				p1 = pitchMedido;
				p2 = pitchMedido;
				p3 = pitchMedido;
				p4 = pitchMedido;

				filtroInicializado = 1;
			}
			else
			{
				r4 = r3;
				r3 = r2;
				r2 = r1;
				r1 = rollMedido;

				p4 = p3;
				p3 = p2;
				p2 = p1;
				p1 = pitchMedido;
			}


			rollFiltrado =
			(int16_t)(
			(
			(int32_t)r1 +
			r2 +
			r3 +
			r4
			) / 4
			);


			pitchFiltrado =
			(int16_t)(
			(
			(int32_t)p1 +
			p2 +
			p3 +
			p4
			) / 4
			);


			// ==========================================
			// CALIBRAR + EEPROM
			// ==========================================

			if (BotonCalibrar())
			{
				offsetRoll =
				rollFiltrado;

				offsetPitch =
				pitchFiltrado;


				eeprom_update_word(
				(uint16_t *)&ee_offsetRoll,
				(uint16_t)offsetRoll
				);

				eeprom_update_word(
				(uint16_t *)&ee_offsetPitch,
				(uint16_t)offsetPitch
				);

				eeprom_update_byte(
				&ee_magic,
				EEPROM_MAGIC
				);


				UART_Texto(
				"CALIBRADO Y GUARDADO R="
				);

				UART_Numero(
				offsetRoll
				);

				UART_Texto(
				" P="
				);

				UART_Numero(
				offsetPitch
				);

				UART_Texto(
				"\r\n"
				);
			}


			// ==========================================
			// APLICAR OFFSET
			// ==========================================

			roll =
			rollFiltrado -
			offsetRoll;

			pitch =
			pitchFiltrado -
			offsetPitch;


			absRoll =
			Absoluto(roll);

			absPitch =
			Absoluto(pitch);


			// ==========================================
			// ZONA MUERTA ±3°
			// ==========================================

			if (
			absRoll <= 3 &&
			absPitch <= 3
			)
			{
				fila = 3;
				columna = 3;

				// AZUL
				rojo = 0;
				verde = 0;
				azul = 100;
			}
			else
			{
				// ======================================
				// POSICION
				// ======================================

				fila =
				Mapear(roll);

				columna =
				Mapear(pitch);


				// ======================================
				// PITCH PREDOMINA -> ROJO
				// ======================================

				if (
				absPitch >
				(absRoll + 3)
				)
				{
					rojo = 100;
					verde = 0;
					azul = 0;
				}


				// ======================================
				// ROLL PREDOMINA -> VERDE
				// ======================================

				else if (
				absRoll >
				(absPitch + 3)
				)
				{
					rojo = 0;
					verde = 100;
					azul = 0;
				}


				// ======================================
				// COMBINADO -> AMARILLO
				// ======================================

				else
				{
					rojo = 100;
					verde = 100;
					azul = 0;
				}
			}
		}


		// ==============================================
		// DETECTAR BORDE
		// ==============================================

		if (
		fila == 0 ||
		fila == 7 ||
		columna == 0 ||
		columna == 7
		)
		{
			enBorde = 1;
		}
		else
		{
			enBorde = 0;

			// Primero debe haber estado
			// dentro de la matriz
			alarmaHabilitada = 1;
		}


		// ==============================================
		// AVISO DE LIMITE
		// ==============================================

		if (
		alarmaHabilitada &&
		enBorde &&
		!enBordeAnterior
		)
		{
			UART_Texto(
			"LIMITE ALCANZADO\r\n"
			);


			// BUZZER ON
			PORTD |=
			(1 << PD3);


			MostrarPixel(
			fila,
			columna,
			rojo,
			verde,
			azul
			);


			_delay_ms(150);


			// PARPADEO
			ApagarMatriz();


			_delay_ms(150);


			// BUZZER OFF
			PORTD &=
			~(1 << PD3);
		}


		enBordeAnterior =
		enBorde;


		// ==============================================
		// MOSTRAR MATRIZ
		// ==============================================

		MostrarPixel(
		fila,
		columna,
		rojo,
		verde,
		azul
		);


		// ==============================================
		// TERMINAL MUCHO MAS LENTO
		// ==============================================

		contadorTerminal++;


		// Antes era >= 5
		// Ahora una línea cada ~1,5 segundos
		if (contadorTerminal >= 10)
		{
			contadorTerminal = 0;


			UART_Texto(
			"RAW R="
			);

			UART_Numero(
			rollMedido
			);

			UART_Texto(
			" P="
			);

			UART_Numero(
			pitchMedido
			);


			UART_Texto(
			" | FILT R="
			);

			UART_Numero(
			rollFiltrado
			);

			UART_Texto(
			" P="
			);

			UART_Numero(
			pitchFiltrado
			);


			UART_Texto(
			" | ROLL="
			);

			UART_Numero(
			roll
			);

			UART_Texto(
			" PITCH="
			);

			UART_Numero(
			pitch
			);


			UART_Texto(
			" | F="
			);

			UART_Numero(
			fila
			);

			UART_Texto(
			" C="
			);

			UART_Numero(
			columna
			);


			if (hold)
			{
				UART_Texto(
				" HOLD=ON"
				);
			}
			else
			{
				UART_Texto(
				" HOLD=OFF"
				);
			}


			if (enBorde)
			{
				UART_Texto(
				" LIMITE"
				);
			}


			UART_Texto(
			"\r\n"
			);
		}


		// La matriz sigue actualizando rápido
		_delay_ms(150);
	}
}

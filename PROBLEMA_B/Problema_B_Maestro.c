#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/power.h>
#include <util/delay.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* PROBLEMA B - MAESTRO ATmega328P, 16 MHz
 * ADC0/PC0: potenciometro de velocidad
 * ADC1/PC1: LDR (detector de piezas; umbrales a calibrar en la maqueta)
 * PD2: DHT11, con resistencia pull-up de 4.7k a 10k
 * PD3: pulsador Start/Stop a GND, con pull-up interno
 * LCD 16x2 de 4 bits: RS=PC2, E=PC3, D4..D7=PD4..PD7, RW=GND
 * SPI: SS=PB2, MOSI=PB3, MISO=PB4, SCK=PB5
 * Protocolo: CMD, VALOR, DUMMY; el tercer byte devuelve ACK/NACK.
 */

#define CMD_START       0x01
#define CMD_STOP        0x02
#define CMD_SPEED       0x03
#define CMD_PISTON      0x04
#define CMD_EMERGENCY   0x05
#define ACK_OK          0xA5
#define ACK_NACK        0xE1

#define LDR_DETECTA     400u
#define LDR_LIBERA      500u
#define TEMP_EMERGENCIA 30u
#define TEMP_REARME     28u

#define LCD_RS PC2
#define LCD_E  PC3

static uint8_t proceso_en_marcha;
static uint8_t emergencia;
static uint8_t comunicacion_ok = 1;
static uint8_t temperatura = 24;
static uint8_t temperatura_valida;
static uint8_t velocidad_pwm;
static uint8_t ultimo_comando = CMD_STOP;
static uint16_t contador_piezas;

static void LCD_PulsoEnable(void)
{
    PORTC |= (1 << LCD_E);
    _delay_us(2);
    PORTC &= ~(1 << LCD_E);
    _delay_us(50);
}

static void LCD_EnviarNibble(uint8_t nibble)
{
    PORTD = (PORTD & 0x0F) | ((nibble & 0x0F) << 4);
    LCD_PulsoEnable();
}

static void LCD_Comando(uint8_t cmd)
{
    PORTC &= ~(1 << LCD_RS);
    LCD_EnviarNibble(cmd >> 4);
    LCD_EnviarNibble(cmd);
    if (cmd == 0x01 || cmd == 0x02) _delay_ms(2);
}

static void LCD_Dato(uint8_t dato)
{
    PORTC |= (1 << LCD_RS);
    LCD_EnviarNibble(dato >> 4);
    LCD_EnviarNibble(dato);
}

static void LCD_Texto16(const char *texto)
{
    uint8_t n = 0;
    while (*texto && n < 16) {
        LCD_Dato((uint8_t)*texto++);
        n++;
    }
    while (n++ < 16) LCD_Dato(' ');
}

static void LCD_Init(void)
{
    DDRC |= (1 << LCD_RS) | (1 << LCD_E);
    DDRD |= 0xF0;
    _delay_ms(40);
    PORTC &= ~((1 << LCD_RS) | (1 << LCD_E));
    LCD_EnviarNibble(0x03); _delay_ms(5);
    LCD_EnviarNibble(0x03); _delay_us(150);
    LCD_EnviarNibble(0x03);
    LCD_EnviarNibble(0x02);
    LCD_Comando(0x28); /* 4 bits, 2 lineas, matriz 5x8 */
    LCD_Comando(0x0C); /* display encendido, cursor apagado */
    LCD_Comando(0x06);
    LCD_Comando(0x01);
}

static void LCD_Pos(uint8_t fila, uint8_t col)
{
    LCD_Comando((fila ? 0xC0 : 0x80) + col);
}

static void ADC_Init(void)
{
    ADMUX = (1 << REFS0); /* referencia AVcc */
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}

static uint16_t ADC_Leer(uint8_t canal)
{
    ADMUX = (ADMUX & 0xF0) | (canal & 0x0F);
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC)) { }
    return ADC;
}

static void SPI_MasterInit(void)
{
    DDRB |= (1 << PB2) | (1 << PB3) | (1 << PB5); /* SS, MOSI, SCK */
    DDRB &= ~(1 << PB4);                          /* MISO */
    PORTB |= (1 << PB2);                          /* SS inactivo */
    SPCR = (1 << SPE) | (1 << MSTR) | (1 << SPR0); /* modo 0, F_CPU/16 */
    SPSR = 0;
}

static uint8_t SPI_Transfer(uint8_t dato)
{
    SPDR = dato;
    while (!(SPSR & (1 << SPIF))) { }
    return SPDR;
}

/* Una trama lógica contiene CMD y VALOR. Un tercer byte dummy abre
 * la ventana de respuesta del esclavo y devuelve ACK_OK o ACK_NACK. */
static uint8_t SPI_EnviarTrama(uint8_t comando, uint8_t valor)
{
    uint8_t ack;
    PORTB &= ~(1 << PB2);
    _delay_us(2);
    (void)SPI_Transfer(comando);
    (void)SPI_Transfer(valor);
    _delay_us(20); /* tiempo de preparacion del ACK por el esclavo */
    ack = SPI_Transfer(0x00);
    PORTB |= (1 << PB2);
    _delay_us(2);
    ultimo_comando = comando;
    return ack == ACK_OK;
}

static uint8_t DHT11_Leer(uint8_t *temp)
{
    uint8_t datos[5] = {0, 0, 0, 0, 0};
    uint8_t i, j;
    uint16_t timeout;

    DDRD |= (1 << PD2);
    PORTD &= ~(1 << PD2);
    _delay_ms(20);
    PORTD |= (1 << PD2);
    _delay_us(30);
    DDRD &= ~(1 << PD2);

    timeout = 0;
    while ((PIND & (1 << PD2)) && timeout++ < 1000) _delay_us(1);
    if (timeout >= 1000) return 0;
    timeout = 0;
    while (!(PIND & (1 << PD2)) && timeout++ < 1000) _delay_us(1);
    if (timeout >= 1000) return 0;
    timeout = 0;
    while ((PIND & (1 << PD2)) && timeout++ < 1000) _delay_us(1);
    if (timeout >= 1000) return 0;

    for (i = 0; i < 5; i++) {
        for (j = 0; j < 8; j++) {
            timeout = 0;
            while (!(PIND & (1 << PD2)) && timeout++ < 300) _delay_us(1);
            if (timeout >= 300) return 0;
            _delay_us(35);
            datos[i] <<= 1;
            if (PIND & (1 << PD2)) datos[i] |= 1;
            timeout = 0;
            while ((PIND & (1 << PD2)) && timeout++ < 300) _delay_us(1);
            if (timeout >= 300) return 0;
        }
    }
    if ((uint8_t)(datos[0] + datos[1] + datos[2] + datos[3]) != datos[4])
        return 0;
    *temp = datos[2];
    return 1;
}

static void LCD_Actualizar(uint16_t ldr)
{
    char linea[17];
    uint8_t pct = (uint16_t)velocidad_pwm * 100UL / 255UL;
    char estado = !comunicacion_ok ? 'X' : (emergencia ? 'E' :
                   (proceso_en_marcha ? 'R' : 'P'));

    LCD_Pos(0, 0);
    if (temperatura_valida)
        snprintf(linea, sizeof(linea), "T%02u V%3u%% P%03u",
                 temperatura, pct, contador_piezas % 1000);
    else
        snprintf(linea, sizeof(linea), "T-- V%3u%% P%03u",
                 pct, contador_piezas % 1000);
    LCD_Texto16(linea);

    LCD_Pos(1, 0);
    snprintf(linea, sizeof(linea), "%c LDR=%4u C=%02X",
             estado, ldr, ultimo_comando);
    LCD_Texto16(linea);
}

static uint8_t EnviarComando(uint8_t comando, uint8_t valor)
{
    if (!SPI_EnviarTrama(comando, valor)) {
        /* Enclava la lógica local en paro y prueba enviar STOP. */
        proceso_en_marcha = 0;
        emergencia = 0;
        comunicacion_ok = 0;
        (void)SPI_EnviarTrama(CMD_STOP, 0);
        return 0;
    }
    return 1;
}

int main(void)
{
    uint16_t pot, ldr;
    uint8_t pieza_armada = 1;
    uint8_t boton_anterior = 1;
    uint8_t temperatura_actual;
    uint8_t velocidad_enviada = 0;
    uint16_t ciclos_dht = 0;
    uint16_t ciclos_reintento = 0;

    clock_prescale_set(clock_div_1);
    DDRD &= ~((1 << PD2) | (1 << PD3));
    PORTD |= (1 << PD3); /* pull-up del pulsador */
    ADC_Init();
    LCD_Init();
    SPI_MasterInit();

    if (!SPI_EnviarTrama(CMD_STOP, 0)) comunicacion_ok = 0;

    while (1) {
        uint8_t boton = (PIND & (1 << PD3)) ? 1 : 0;
        pot = ADC_Leer(0);
        ldr = ADC_Leer(1);
        velocidad_pwm = (uint32_t)pot * 255UL / 1023UL;

        if (!comunicacion_ok) {
            if (++ciclos_reintento >= 50) { /* reintento cada ~0.5 s */
                ciclos_reintento = 0;
                if (SPI_EnviarTrama(CMD_STOP, 0)) {
                    comunicacion_ok = 1;
                    proceso_en_marcha = 0;
                    emergencia = 0;
                    velocidad_enviada = 0;
                }
            }
        } else {
            if (boton_anterior && !boton) {
                _delay_ms(25);
                if (!(PIND & (1 << PD3))) {
                    if (emergencia) {
                        if (temperatura_valida && temperatura <= TEMP_REARME) {
                            if (EnviarComando(CMD_STOP, 0)) {
                                emergencia = 0;
                                proceso_en_marcha = 0;
                            }
                        }
                    } else if (proceso_en_marcha) {
                        if (EnviarComando(CMD_STOP, 0)) {
                            proceso_en_marcha = 0;
                            velocidad_enviada = 0;
                        }
                    } else if (temperatura_valida &&
                               temperatura < TEMP_EMERGENCIA) {
                        if (EnviarComando(CMD_START, velocidad_pwm)) {
                            proceso_en_marcha = 1;
                            velocidad_enviada = velocidad_pwm;
                        }
                    }
                }
            }
            boton_anterior = boton;

            if (++ciclos_dht >= 100) { /* aproximación a 1 s; loop base 10 ms */
                ciclos_dht = 0;
                if (DHT11_Leer(&temperatura_actual)) {
                    temperatura = temperatura_actual;
                    temperatura_valida = 1;
                }
                if (temperatura_valida && !emergencia &&
                    temperatura >= TEMP_EMERGENCIA) {
                    if (EnviarComando(CMD_EMERGENCY, temperatura)) {
                        emergencia = 1;
                        proceso_en_marcha = 0;
                    }
                }
            }

            if (proceso_en_marcha && !emergencia && comunicacion_ok) {
                if (velocidad_pwm != velocidad_enviada) {
                    if (EnviarComando(CMD_SPEED, velocidad_pwm))
                        velocidad_enviada = velocidad_pwm;
                }
                if (comunicacion_ok && pieza_armada && ldr < LDR_DETECTA) {
                    pieza_armada = 0;
                    contador_piezas++;
                    (void)EnviarComando(CMD_PISTON, 1);
                } else if (ldr > LDR_LIBERA) {
                    pieza_armada = 1;
                }
            }
        }
        LCD_Actualizar(ldr);
        _delay_ms(10);
    }
}

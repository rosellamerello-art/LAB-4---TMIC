#define F_CPU 16000000UL
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <util/atomic.h>
#include <stdint.h>

/* PROBLEMA B - ESCLAVO ATmega328P, 16 MHz
 * SPI: SS=PB2, MOSI=PB3, MISO=PB4, SCK=PB5 (modo 0, master F_CPU/16)
 * Motor: PD5/OC0B -> entrada PWM del L293D/L298N; GND comun obligatorio
 * Servo SG90: PB1/OC1A; LED rojo PD2; buzzer PD3; LED verde PD4
 * Deben usarse fuentes adecuadas para motor/servo y no alimentar cargas
 * directamente desde los pines del ATmega328P.
 * Protocolo: recibe CMD, VALOR; carga ACK/NACK y recibe un byte dummy.
 */

#define CMD_START       0x01
#define CMD_STOP        0x02
#define CMD_SPEED       0x03
#define CMD_PISTON      0x04
#define CMD_EMERGENCY   0x05
#define ACK_OK          0xA5
#define ACK_NACK        0xE1

#define PIN_SS          PB2
#define MOTOR_PWM       PD5
#define LED_ROJO        PD2
#define BUZZER          PD3
#define LED_VERDE       PD4

static volatile uint32_t system_ms;
static uint8_t marcha;
static uint8_t emergencia;
static uint8_t velocidad;
static volatile uint8_t servo_activo;
static volatile uint32_t servo_deadline;

/* Timer1 genera el periodo de servo de 20 ms. Su overflow actua como
 * base de tiempo para una temporizacion no bloqueante del servo. */
ISR(TIMER1_OVF_vect)
{
    system_ms += 20;
    if (servo_activo && (int32_t)(system_ms - servo_deadline) >= 0) {
        OCR1A = 250;
        servo_activo = 0;
    }
}

static uint32_t TiempoMs(void)
{
    uint32_t copia;
    ATOMIC_BLOCK(ATOMIC_RESTORESTATE) { copia = system_ms; }
    return copia;
}

static void Motor_Init(void)
{
    DDRD |= (1 << MOTOR_PWM);
    /* Fast PWM OC0B, prescaler 8: PWM cercana a 7.8 kHz. */
    TCCR0A = (1 << COM0B1) | (1 << WGM01) | (1 << WGM00);
    TCCR0B = (1 << CS01);
    OCR0B = 0;
}

static void Motor_Set(uint8_t pwm)
{
    OCR0B = pwm;
}

static void Servo_Init(void)
{
    DDRB |= (1 << PB1);
    /* Fast PWM modo 14, TOP=4999 con prescaler 64: 50 Hz.
     * 250 cuentas ~=1.0 ms; 500 cuentas ~=2.0 ms. Ajustar al mecanismo. */
    TCCR1A = (1 << COM1A1) | (1 << WGM11);
    TCCR1B = (1 << WGM13) | (1 << WGM12) | (1 << CS11) | (1 << CS10);
    ICR1 = 4999;
    OCR1A = 250;
    TIMSK1 |= (1 << TOIE1);
}

static void Servo_Reposo(void)
{
    OCR1A = 250;
    servo_activo = 0;
}

static void Servo_Accionar(void)
{
    OCR1A = 500;
    servo_deadline = TiempoMs() + 700UL;
    servo_activo = 1;
}

static void Buzzer_On(void)
{
    DDRD |= (1 << BUZZER);
    /* PWM audible en OC2B, modo Fast PWM con TOP=OCR2A. */
    OCR2A = 124;
    OCR2B = 62;
    TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
    TCCR2B = (1 << WGM22) | (1 << CS22);
}

static void Buzzer_Off(void)
{
    TCCR2A = 0;
    TCCR2B = 0;
    PORTD &= ~(1 << BUZZER);
}

static void Estado_Paro(void)
{
    marcha = 0;
    Motor_Set(0);
    Servo_Reposo();
    PORTD &= ~((1 << LED_VERDE) | (1 << LED_ROJO));
    Buzzer_Off();
}

static void Estado_Marcha(void)
{
    emergencia = 0;
    marcha = 1;
    PORTD |= (1 << LED_VERDE);
    PORTD &= ~(1 << LED_ROJO);
    Buzzer_Off();
    Motor_Set(velocidad);
}

static void Estado_Emergencia(void)
{
    emergencia = 1;
    marcha = 0;
    Motor_Set(0);
    Servo_Reposo();
    PORTD &= ~(1 << LED_VERDE);
    PORTD |= (1 << LED_ROJO);
    Buzzer_On();
}

static void SPI_SlaveInit(void)
{
    DDRB &= ~((1 << PIN_SS) | (1 << PB3) | (1 << PB5));
    DDRB |= (1 << PB4); /* MISO */
    PORTB |= (1 << PIN_SS); /* pull-up hasta que el maestro controle SS */
    SPCR = (1 << SPE);       /* modo 0, MSB primero */
    SPSR = 0;
    SPDR = 0x00;
}

/* Espera un byte dentro de una seleccion SS activa. Retorna 0 si SS
 * sube antes de que termine el byte; el llamador descarta esa trama. */
static uint8_t SPI_RecibirByte(uint8_t *dato)
{
    while (PINB & (1 << PIN_SS)) { }
    while (!(SPSR & (1 << SPIF))) {
        if (PINB & (1 << PIN_SS)) return 0;
    }
    *dato = SPDR;
    return 1;
}

static uint8_t ComandoValido(uint8_t comando, uint8_t valor)
{
    switch (comando) {
        case CMD_START:     return !emergencia;
        case CMD_STOP:      return 1;
        case CMD_SPEED:     return marcha && !emergencia;
        case CMD_PISTON:    return marcha && !emergencia && valor == 1;
        case CMD_EMERGENCY: return 1;
        default:            return 0;
    }
}

static void ProcesarComando(uint8_t comando, uint8_t valor)
{
    switch (comando) {
        case CMD_START:
            velocidad = valor;
            Estado_Marcha();
            break;
        case CMD_STOP:
            emergencia = 0;
            Estado_Paro();
            break;
        case CMD_SPEED:
            velocidad = valor;
            Motor_Set(velocidad);
            break;
        case CMD_PISTON:
            Servo_Accionar();
            break;
        case CMD_EMERGENCY:
            Estado_Emergencia();
            break;
        default:
            break;
    }
}

int main(void)
{
    uint8_t comando, valor, dummy;
    clock_prescale_set(clock_div_1);
    DDRD |= (1 << LED_ROJO) | (1 << LED_VERDE);
    PORTD &= ~((1 << LED_ROJO) | (1 << LED_VERDE));
    Motor_Init();
    Servo_Init();
    SPI_SlaveInit();
    Estado_Paro();
    sei();

    while (1) {
        if (!SPI_RecibirByte(&comando)) continue;
        if (!SPI_RecibirByte(&valor)) continue;

        /* El ACK se carga antes de que el maestro genere el tercer byte. */
        SPDR = ComandoValido(comando, valor) ? ACK_OK : ACK_NACK;
        if (!SPI_RecibirByte(&dummy)) continue;
        (void)dummy;

        if (ComandoValido(comando, valor))
            ProcesarComando(comando, valor);

    }
}

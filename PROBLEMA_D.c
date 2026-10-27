#define F_CPU 16000000UL

#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/power.h>
#include <avr/eeprom.h>
#include <util/delay.h>
#include <stdint.h>
#define MAX_NIVEL 10
#define COLOR_ROJO      0
#define COLOR_VERDE     1
#define COLOR_AZUL      2
#define COLOR_AMARILLO  3
uint8_t secuencia[MAX_NIVEL];
uint8_t colores[MAX_NIVEL];
uint16_t lfsr = 0xACE1u;
uint8_t EEMEM ee_nivel;
uint8_t EEMEM ee_record;

/*==WS2812B - DATA EN PD6==*/
static void WS2812_EnviarByte(uint8_t dato)

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
        : [dato] "+r" (dato), [contador] "+r" (contador)
        : [puerto] "I" (_SFR_IO_ADDR(PORTD)), [pin] "I" (PD6)
    );
}
static void WS2812_Pixel(uint8_t r, uint8_t g, uint8_t b)
{
    WS2812_EnviarByte(g);
    WS2812_EnviarByte(r);
    WS2812_EnviarByte(b);
}
static void WS2812_Reset(void)
{
    PORTD &= ~(1 << PD6);
    _delay_us(100);
}

/*= ADC joystick =*/
static void ADC_Init(void)
{
    ADMUX = (1 << REFS0);
    ADCSRA = (1 << ADEN) | (1 << ADPS2) | (1 << ADPS1) | (1 << ADPS0);
}
static uint16_t ADC_Leer(uint8_t canal)
{
    ADMUX = (ADMUX & 0xF0) | canal;
    ADCSRA |= (1 << ADSC);
    while (ADCSRA & (1 << ADSC));
    return ADC;
}
/*=Buzzer PD3 / OC2B=*/
static void Buzzer_Iniciar(uint8_t color)
{
    uint8_t top;
    switch (color)
    {
        case COLOR_ROJO:     top = 238; break;
        case COLOR_VERDE:    top = 189; break;
        case COLOR_AZUL:     top = 159; break;
        default:             top = 126; break;
    }
    OCR2A = top;
    OCR2B = top / 2;
    TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
    TCCR2B = (1 << WGM22) | (1 << CS22) | (1 << CS20);
}
static void Buzzer_Parar(void)
{
    TCCR2A = 0;
    TCCR2B = 0;
    PORTD &= ~(1 << PD3);
}
static void Buzzer_Error(void)
{
    OCR2A = 249;
    OCR2B = 124;
    TCCR2A = (1 << COM2B1) | (1 << WGM21) | (1 << WGM20);
    TCCR2B = (1 << WGM22) | (1 << CS22) | (1 << CS21);
}

/*=Matriz=*/

static void MostrarPixel(uint8_t posicion, uint8_t r, uint8_t g, uint8_t b)
{
    uint8_t i;
    cli();
    for (i = 0; i < 64; i++)
    {
        if (i == posicion) WS2812_Pixel(r, g, b);
        else WS2812_Pixel(0,0,0);
    }
    sei();
    WS2812_Reset();
}
static void MostrarPixelColor(uint8_t posicion, uint8_t color)
{
    switch (color)
    {
        case COLOR_ROJO:     MostrarPixel(posicion,100,0,0); break;
        case COLOR_VERDE:    MostrarPixel(posicion,0,100,0); break;
        case COLOR_AZUL:     MostrarPixel(posicion,0,0,100); break;
        case COLOR_AMARILLO: MostrarPixel(posicion,100,100,0); break;
    }
}
static void ApagarMatriz(void)
{
    uint8_t i;
    cli();
    for (i = 0; i < 64; i++) WS2812_Pixel(0,0,0);
    sei();
    WS2812_Reset();
}
static void MatrizColor(uint8_t r, uint8_t g, uint8_t b)
{
   uint8_t i;
    cli();
    for (i = 0; i < 64; i++) WS2812_Pixel(r,g,b);
    sei();
    WS2812_Reset();
}

static void MostrarRecord(uint8_t record)
{
    uint8_t i;
    if (record > MAX_NIVEL) record = MAX_NIVEL;
    cli();
    for (i = 0; i < 64; i++)
    {
        if (i < record) WS2812_Pixel(0,100,0);
        else WS2812_Pixel(0,0,0);
    }
    sei();
    WS2812_Reset();
    _delay_ms(1500);
    ApagarMatriz();
    _delay_ms(300);
}
static void ErrorRojo(void)
{
    uint8_t i;
    Buzzer_Error();
    for (i = 0; i < 3; i++)
    {
        MatrizColor(100,0,0);
        _delay_ms(250);
        ApagarMatriz();
        _delay_ms(250);
    }
    Buzzer_Parar();
}
static void ExitoVerde(void)
{
    uint8_t i;
    for (i = 0; i < 2; i++)
    {
        MatrizColor(0,80,0);
        _delay_ms(200);
        ApagarMatriz();
        _delay_ms(200);
    }
}

/*=Pseudoaleatorio=*/

static uint8_t NumeroAleatorio(void)
{
    uint16_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
    lfsr = (lfsr >> 1) | (bit << 15);
    return (uint8_t)(lfsr % 64);
}
static uint8_t ColorAleatorio(void)
{
    uint16_t bit = ((lfsr >> 0) ^ (lfsr >> 2) ^ (lfsr >> 3) ^ (lfsr >> 5)) & 1;
    lfsr = (lfsr >> 1) | (bit << 15);
    return (uint8_t)(lfsr % 4);
}

static void GenerarSecuencia(uint8_t cantidad)
{
    uint8_t i;
    for (i = 0; i < cantidad; i++)
    {
        secuencia[i] = NumeroAleatorio();
        colores[i] = ColorAleatorio();
    }
}
static void MostrarSecuencia(uint8_t nivel)
{
    uint8_t i;
    ApagarMatriz();
    _delay_ms(500);
    for (i = 0; i < nivel; i++)
    {
        MostrarPixelColor(secuencia[i], colores[i]);
        Buzzer_Iniciar(colores[i]);
        _delay_ms(500);
        Buzzer_Parar();
        ApagarMatriz();
        _delay_ms(200);
    }
}

/*=Modo Libre=*/

static void ModoLibre(void)
{
    uint8_t fila = 3;
    uint8_t columna = 3;
    uint8_t posicionActual;

    uint8_t colorCursor = COLOR_AZUL;
    uint8_t nuevoColor;

    uint8_t movimientoPermitido = 1;

    uint16_t ejeX;
    uint16_t ejeY;

    uint8_t tiempo;

    posicionActual = fila * 8 + columna;

    MostrarPixelColor(
        posicionActual,
        colorCursor
    );

    // Modo libre durante unos 4 segundos
    for (tiempo = 0; tiempo < 80; tiempo++)
    {
        ejeX = ADC_Leer(0);
        ejeY = ADC_Leer(1);

        // DERECHA
        if (movimientoPermitido && ejeX > 700)
        {
            if (columna < 7)
                columna++;

            movimientoPermitido = 0;
        }

        // IZQUIERDA
        else if (movimientoPermitido && ejeX < 300)
        {
            if (columna > 0)
                columna--;

            movimientoPermitido = 0;
        }

        // ABAJO
        else if (movimientoPermitido && ejeY < 300)
        {
            if (fila < 7)
                fila++;

            movimientoPermitido = 0;
        }

        // ARRIBA
        else if (movimientoPermitido && ejeY > 700)
        {
            if (fila > 0)
                fila--;

            movimientoPermitido = 0;
        }

        posicionActual =
            fila * 8 + columna;

        MostrarPixelColor(
            posicionActual,
            colorCursor
        );

        // Joystick vuelve al centro
        if (
            ejeX >= 300 &&
            ejeX <= 700 &&
            ejeY >= 300 &&
            ejeY <= 700
        )
        {
            movimientoPermitido = 1;
        }

        // D2 EN MODO LIBRE = CAMBIAR COLOR
        if (!(PIND & (1 << PD2)))
        {
            _delay_ms(30);

            if (!(PIND & (1 << PD2)))
            {
                do
                {
                    nuevoColor =
                        ColorAleatorio();

                } while (
                    nuevoColor ==
                    colorCursor
                );

                colorCursor =
                    nuevoColor;

                MostrarPixelColor(
                    posicionActual,
                    colorCursor
                );

                Buzzer_Iniciar(
                    colorCursor
                );

                _delay_ms(150);

                Buzzer_Parar();

                while (!(PIND & (1 << PD2)));

                _delay_ms(50);
            }
        }

        _delay_ms(50);
    }

    ApagarMatriz();
    _delay_ms(300);
}
int main(void)
{
    uint8_t nivel;
    uint8_t record;
    uint8_t indiceJugador;
    uint8_t fila;
    uint8_t columna;
    uint8_t posicionActual;
    uint16_t ejeX;
    uint16_t ejeY;
    uint8_t error;
     cli();
    clock_prescale_set(clock_div_1);
    sei();

    // Matriz D6

    DDRD |= (1 << PD6);
    PORTD &= ~(1 << PD6);

    // Buzzer D3

    DDRD |= (1 << PD3);
    PORTD &= ~(1 << PD3);
 
   // BTN A D2

    DDRD &= ~(1 << PD2);
    PORTD |= (1 << PD2);
    ADC_Init();
    _delay_ms(500);
    nivel = eeprom_read_byte(&ee_nivel);
    record = eeprom_read_byte(&ee_record);
    if (nivel == 0xFF || nivel == 0 || nivel > MAX_NIVEL)
    {
        nivel = 1;
        eeprom_update_byte(&ee_nivel, nivel);
    }
 
    if (record == 0xFF || record > MAX_NIVEL)
    {
        record = 0;
        eeprom_update_byte(&ee_record, record);
    }
 
    MostrarRecord(record);

    lfsr ^= ADC_Leer(0);
    lfsr ^= ((uint16_t)ADC_Leer(1) << 5);
    GenerarSecuencia(nivel);

 while (1)
{
    ModoLibre();

    MostrarSecuencia(nivel);
        fila = 3;
        columna = 3;
        posicionActual = fila * 8 + columna;
        MostrarPixel(posicionActual,80,0,0);
        indiceJugador = 0;
        error = 0;
        while (indiceJugador < nivel)
        {
            ejeX = ADC_Leer(0);
            ejeY = ADC_Leer(1);

            if (ejeX > 700)
            {
                if (columna < 7) columna++;
                posicionActual = fila * 8 + columna;
                MostrarPixel(posicionActual,80,0,0);
                _delay_ms(250);
            }
            else if (ejeX < 300)
            {
                if (columna > 0) columna--;
                posicionActual = fila * 8 + columna;
                MostrarPixel(posicionActual,80,0,0);
                _delay_ms(250);
            }
            else if (ejeY < 300) // Y invertido en PICSimLab
            {
                if (fila < 7) fila++;
                posicionActual = fila * 8 + columna;
                MostrarPixel(posicionActual,80,0,0);
                _delay_ms(250);
            }
            else if (ejeY > 700)
            {
                if (fila > 0) fila--;
                posicionActual = fila * 8 + columna;
                MostrarPixel(posicionActual,80,0,0);
                _delay_ms(250);
            }
 
            if (!(PIND & (1 << PD2)))
            {
                posicionActual = fila * 8 + columna;
                if (posicionActual == secuencia[indiceJugador])

                {
                    MostrarPixel(posicionActual,0,100,0);
                    _delay_ms(300);
                    indiceJugador++;
 
                    while (!(PIND & (1 << PD2)));
                    _delay_ms(50);
                    if (indiceJugador < nivel)
                        MostrarPixel(posicionActual,80,0,0);
                }
                else
                {
                    error = 1;
                    while (!(PIND & (1 << PD2)));
                    break;
                }
            }
        }
 
        if (error)
        {
            ErrorRojo();
            nivel = 1;
            eeprom_update_byte(&ee_nivel, nivel);
            GenerarSecuencia(1);
            _delay_ms(500);
        }
        else
        {
            ExitoVerde();
            if (nivel > record)
            {
                record = nivel;
                eeprom_update_byte(&ee_record, record);
            }

            if (nivel < MAX_NIVEL)
            {
                secuencia[nivel] = NumeroAleatorio();
                colores[nivel] = ColorAleatorio();
                nivel++;
                eeprom_update_byte(&ee_nivel, nivel);
            }
            else
            {
                MatrizColor(100,100,0);
                _delay_ms(1000);
                ApagarMatriz();
                record = MAX_NIVEL;
                eeprom_update_byte(&ee_record, record);
                nivel = 1;
                eeprom_update_byte(&ee_nivel, nivel);
                GenerarSecuencia(1);

            }

            _delay_ms(600);
        }
    }
}

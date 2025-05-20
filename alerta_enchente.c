#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/adc.h"
#include "hardware/i2c.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "lib/ssd1306.h"
#include "lib/font.h"
#include "FreeRTOS.h"
#include "FreeRTOSConfig.h"
#include "task.h"
#include "queue.h"
#include <stdio.h>

//arquivo .pio
#include "led_matrix.pio.h"

//definições de portas e pinos
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define endereco 0x3C

#define LED_RED 13
#define LED_GREEN 11
#define LED_BLUE 12
#define BUZZER 10
#define BUTTON_A 5
#define OUT_PIN 7 //GPIO matriz de leds
#define ADC_JOYSTICK_X 26
#define ADC_JOYSTICK_Y 27

//protótipos
uint32_t matrix_rgb(uint8_t b, uint8_t r, uint8_t g);
void desenho_pio(bool modo, PIO pio, uint sm);

//estrutura para armazenar dados dos sensores
typedef struct
{
    float nivel_agua;   //nível da água (%)
    float vol_chuva;    //volume de chuva (%)
} sensor_data_t;

//xQueueSensorData: fila de dados lidos dos sensores pelo ADC (sensordata)
QueueHandle_t xQueueSensorData;

//xQueueModo: fila de booleano que indica modo de operação (modo 0 -> estado normal ou modo = 1 -> estado de alerta)
QueueHandle_t xQueueModo;

//função para leitura de dados nos sensores
//os dados são lidos e enviados para fila (xQueueSensorData)
void vSensorTask(void *params)
{
    //inicializa ADC
    adc_gpio_init(ADC_JOYSTICK_Y);
    adc_gpio_init(ADC_JOYSTICK_X);
    adc_init();

    sensor_data_t sensordata;

    while (true)
    {
        adc_select_input(0); // GPIO 26 = ADC0
        sensordata.vol_chuva = adc_read()*(100.2f/4096.2f); //converte valor lido para faixa de 0 a 100

        adc_select_input(1); // GPIO 27 = ADC1
        sensordata.nivel_agua = adc_read()*(100.2f/4096.2f); //converte valor lido para faixa de 0 a 100

        printf("%i e %i\n", sensordata.vol_chuva, sensordata.nivel_agua);
        xQueueSend(xQueueSensorData, &sensordata, 0); // Envia o valor dos sensores para a fila
        vTaskDelay(pdMS_TO_TICKS(10));         
    }
}

//tarefa para indicar modo de operação e envia o flag por fila
//se nivel_agua maior ou igual a 70 ou vol_chuva maior ou igual 80 -> modo = 1 (estado de alerta de enchentes)
//caso contrário -> modo = 0 (estado normal de operação)
void vIndicaModoTask(void *params)
{
    sensor_data_t sensordata;
    bool modo = 0;

    while(1){
        if(xQueueReceive(xQueueSensorData, &sensordata, portMAX_DELAY) == pdTRUE){
            if(sensordata.nivel_agua >= 70 || sensordata.vol_chuva >= 80){
                modo = 1;
            }
            else{
                modo = 0;
            }
            xQueueSend(xQueueModo, &modo, 0); //envia valor de modo para fila
        }
    } 
}

//tarefa para acender led vermelho em modo 1 de funcionamento (alerta)
void vAlertaLedTask(void *params)
{
    bool modo;

    //inicialização do LED RGB
    gpio_init(LED_RED);
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_put(LED_RED, 0);

    while (true)
    {
        if(xQueueReceive(xQueueModo, &modo, portMAX_DELAY) == pdTRUE){
            gpio_put(LED_RED, modo);
        } 
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//tarefa para emitir sinal sonoro no buzzer (GPIO 10) em estado de alerta (modo 1)
void vSinaisSonorosTask(void *params)
{
    bool modo;

    //inicializa PWM
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    pwm_set_clkdiv(5, 125.0); 
    pwm_set_wrap(5, 1999); 
    pwm_set_gpio_level(BUZZER, 0); 
    pwm_set_enabled(5, true); 

    while (true)
    {
        xQueueReceive(xQueueModo, &modo, portMAX_DELAY);
        modo ? pwm_set_gpio_level(BUZZER, 1000) : pwm_set_gpio_level(BUZZER, 0);
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//tarefa para mostrar informações no display
//envia mensagem de alerta em caso de risco de enchentes (modo)
//e dados dos sensores em ambos os modos de funcionamento
void vDisplayTask(void *params)
{
    bool modo;
    sensor_data_t sensordata;

    char buffer1[4], buffer2[4];

    // I2C Initialisation. Using it at 400Khz.
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);                    // Set the GPIO pin function to I2C
    gpio_pull_up(I2C_SDA);                                        // Pull up the data line
    gpio_pull_up(I2C_SCL);                                        // Pull up the clock line
    ssd1306_t ssd;                                                // Inicializa a estrutura do display
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, endereco, I2C_PORT); // Inicializa o display
    ssd1306_config(&ssd);                                         // Configura o display
    ssd1306_send_data(&ssd);                                      // Envia os dados para o display
    // Limpa o display. O display inicia com todos os pixels apagados.
    ssd1306_fill(&ssd, false);
    ssd1306_send_data(&ssd);

    while (true)
    {
        xQueueReceive(xQueueModo, &modo, portMAX_DELAY);
        xQueueReceive(xQueueSensorData, &sensordata, portMAX_DELAY);
        if(modo){
            ssd1306_fill(&ssd, false);                          // Limpa o display
            ssd1306_line(&ssd, 0, 22, 123, 22, 1);
            ssd1306_draw_string(&ssd, "ALERTA!!!", 30, 2); // Desenha uma string
            ssd1306_draw_string(&ssd, "Risc", 2, 13);
            ssd1306_draw_string(&ssd, "de", 39, 13);
            ssd1306_draw_string(&ssd, "enchente", 59, 13);

            
        }
        else{
            ssd1306_fill(&ssd, false);
            ssd1306_draw_string(&ssd, "Leitura Sensors", 2, 2); // Desenha uma string
        }
            ssd1306_rect(&ssd, 0, 0, 124, 64, 1, 0);
            ssd1306_line(&ssd, 0, 11, 123, 11, 1);

            ssd1306_draw_string(&ssd, "Nvl", 2, 30);
            ssd1306_draw_string(&ssd, "de", 30, 30);
            ssd1306_draw_string(&ssd, "agua", 50, 30);
            ssd1306_draw_string(&ssd, ":", 80, 30);

            ssd1306_draw_string(&ssd, "Vol", 2, 45);
            ssd1306_draw_string(&ssd, "de", 30, 45);
            ssd1306_draw_string(&ssd, "chvs", 50, 45);
            ssd1306_draw_string(&ssd, ":", 80, 45);

            sprintf(buffer1, "%.0f", sensordata.nivel_agua);
            sprintf(buffer2, "%.0f", sensordata.vol_chuva);
            ssd1306_draw_string(&ssd, buffer1, 92, 30); // Desenha uma string
            ssd1306_draw_string(&ssd, buffer2, 92, 45); // Desenha uma string
            ssd1306_send_data(&ssd);

        vTaskDelay(pdMS_TO_TICKS(100));
    }
}


//mostra sinal luminoso de alerta nas matriz de LEDs (!) em caso de risco de risco de enchentes (modo 1) 
void vAlertaMatrizTask(void *params)
{
    bool modo;

    //inicialização matriz de leds
    PIO pio = pio0; //seleciona a pio0
    //set_sys_clock_khz(128000, false); //coloca a frequência de clock para 128 MHz, facilitando a divisão pelo clock
    uint offset = pio_add_program(pio, &pio_matrix_program);
    uint sm = pio_claim_unused_sm(pio, true);
    pio_matrix_program_init(pio, sm, offset, OUT_PIN);

    while (true)
    {
        if(xQueueReceive(xQueueModo, &modo, portMAX_DELAY) == pdTRUE){
            desenho_pio(modo, pio, sm);
        } 
        vTaskDelay(pdMS_TO_TICKS(100));
    }
}

//função principal: cria filas e tarefas para o funcionamento do sistema
int main()
{
    stdio_init_all();

    //cria a fila para compartilhamento de valor dos "sensores" e modo de funcionamento
    xQueueSensorData = xQueueCreate(5, sizeof(sensor_data_t));
    xQueueModo = xQueueCreate(5, sizeof(bool));

    xTaskCreate(vAlertaLedTask, "Liga Led Task", configMINIMAL_STACK_SIZE,
         NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vIndicaModoTask, "Altera Modo Task", configMINIMAL_STACK_SIZE, 
        NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vSensorTask, "Leitura de Sensores Task", configMINIMAL_STACK_SIZE, 
        NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vSinaisSonorosTask, "Sinal Sonoro Task", configMINIMAL_STACK_SIZE, 
        NULL, tskIDLE_PRIORITY, NULL);
    xTaskCreate(vDisplayTask, "Exibe Display Task", configMINIMAL_STACK_SIZE, 
        NULL, tskIDLE_PRIORITY, NULL);  
    xTaskCreate(vAlertaMatrizTask, "Alerta Matriz Task", configMINIMAL_STACK_SIZE, 
        NULL, tskIDLE_PRIORITY, NULL);   
    vTaskStartScheduler();
    panic_unsupported();
}

//rotina para definição da intensidade de cores do led na matriz 5x5
uint32_t matrix_rgb(uint8_t r, uint8_t g, uint8_t b)
{
  return (g << 24) | (r << 16) | (b << 8);
}

//rotina para acionar a matrix de leds - ws2812b
void desenho_pio(bool modo, PIO pio, uint sm){
    for (int16_t i = 0; i < 25; i++) {
        if (modo){
            (i%5 == 2 && i != 7) ? pio_sm_put_blocking(pio, sm, matrix_rgb(5, 5, 5)) : pio_sm_put_blocking(pio, sm, matrix_rgb(15, 0, 0));
        }
        else {
            pio_sm_put_blocking(pio, sm, matrix_rgb(0, 0, 0));
        }
    }
}
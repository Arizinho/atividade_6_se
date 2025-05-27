#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "hardware/pwm.h"
#include "lib/ssd1306.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "pico/bootrom.h"

#define MAX_USERS 8 //número máximo de usuários simutâneoes

#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define ENDERECO 0x3C

#define LED_RED 13
#define LED_GREEN 11
#define LED_BLUE 12
#define BUZZER 10

#define BOTAO_A 5
#define BOTAO_B 6
#define BOTAO_JOYSTICK 22

ssd1306_t ssd;

//definição de semáforo
SemaphoreHandle_t xAddUserSem;
SemaphoreHandle_t xRemoveUserSem;
SemaphoreHandle_t xResetUserSem;
SemaphoreHandle_t xDisplayMutex;

//variável para guardar número de usuários
uint8_t usuarios = 0;

//protótipos
void atualizar_led();
void gpio_irq_handler(uint gpio, uint32_t events);
void gpio_callback(uint gpio, uint32_t events);
void setup_gpios();
void beep_curto();

/*
Tarefa para indicar entrada de usuário
gerida por semáforo de contagem (xAddUserSem)
incrementa valor de usuario em 1 por contagem do semáforo
ou aciona beep caso tenha alcançado valor máximo
*/
void vTaskEntrada(void *params) {
    while (true) {
        if (xSemaphoreTake(xAddUserSem, 0) == pdTRUE) {
            //incrementa usuario se valor for menor que o máximo 
            if(usuarios < MAX_USERS){
                usuarios++;
            } 
            //emite beep se valor de usuario tiver atingido máximo
            else {
                beep_curto();
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*
Tarefa para indicar saída de usuário
gerida por semáforo de contagem (xRemoveUserSem)
decrementa valor de usuario em 1 por contagem do semáforo 
*/
void vTaskSaida(void *params) {
    while (true) {
        if (xSemaphoreTake(xRemoveUserSem, 0) == pdTRUE) {
            //decrementa usuario se valor for menor que 0
            if(usuarios > 0){
                usuarios--;
            }
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/*
Tarefa para resetar valor de usuário
gerida por semáforo de binário (xResetUserSem)
zera valor de usuario e emite beep duplo
*/
void vTaskReset(void *params) {
    while (true) {
        if (xSemaphoreTake(xResetUserSem, portMAX_DELAY) == pdTRUE) {
            usuarios = 0;

            //beep duplo
            beep_curto();
            vTaskDelay(pdMS_TO_TICKS(100));
            beep_curto();
        }
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

/*
Tarefa para mostra visualmente número de usuários atual
execução protegida pelo mutex (xDisplayMutex)
exibe valor de usuário em display OLED
e emite luz no led RGB de acordo com o valor
    *azul -> 0 usuários
    *verde -> 1 a max-2 usuários
    *amarelo -> max-1 usuários
    *vermelho -> valor max de usuários
*/
void vTaskDisplay(void *params) {
    char buffer[20];

    //configura GPIO do LED RGB
    gpio_init(LED_RED); 
    gpio_set_dir(LED_RED, GPIO_OUT);
    gpio_put(LED_RED, 0);

    gpio_init(LED_GREEN); 
    gpio_set_dir(LED_GREEN, GPIO_OUT);
    gpio_put(LED_GREEN, 0);
    
    gpio_init(LED_BLUE); 
    gpio_set_dir(LED_BLUE, GPIO_OUT);
    gpio_put(LED_BLUE, 0);
    
    while (true) {
        if (xSemaphoreTake(xDisplayMutex, portMAX_DELAY)==pdTRUE) {
            //exibe usuários no display
            ssd1306_fill(&ssd, false);
            ssd1306_rect(&ssd, 0, 0, 124, 64, 1, 0);
            ssd1306_line(&ssd, 0, 11, 123, 11, 1);
            sprintf(buffer, "Usuarios: %u", usuarios);
            ssd1306_draw_string(&ssd, "Controle Acesso", 3, 2);
            ssd1306_draw_string(&ssd, buffer, 10, 20);
            if(usuarios == 0){
                ssd1306_draw_string(&ssd, "Ambiente vazio", 5, 40);
            }
            else if(usuarios <= MAX_USERS-2){
                ssd1306_draw_string(&ssd, "Vagas dispon.", 5, 40);
            }
            else if(usuarios == MAX_USERS-1){
                ssd1306_draw_string(&ssd, "Ultima vaga", 5, 40);
            }
            else{
                ssd1306_draw_string(&ssd, "Amb. lotado", 5, 40);
            }

            ssd1306_send_data(&ssd);

            //mostra cor de led correspondente ao n. de usuários
            atualizar_led();

            xSemaphoreGive(xDisplayMutex);
        }
        vTaskDelay(pdMS_TO_TICKS(200));
    }
}

int main() {
    stdio_init_all();

    //inicializa gpios
    setup_gpios();

    // Inicialização do display
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    ssd1306_init(&ssd, WIDTH, HEIGHT, false, ENDERECO, I2C_PORT);
    ssd1306_config(&ssd);
    ssd1306_send_data(&ssd);

    //habilita interrupções nos botões A, B e do Joystick
    gpio_set_irq_enabled_with_callback(BOTAO_A, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_B, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    gpio_set_irq_enabled_with_callback(BOTAO_JOYSTICK, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);

    //cria semáforos
    xAddUserSem = xSemaphoreCreateCounting(MAX_USERS, 0);
    xRemoveUserSem = xSemaphoreCreateCounting(MAX_USERS, 0);
    xResetUserSem = xSemaphoreCreateBinary();
    xDisplayMutex = xSemaphoreCreateMutex();

    //cria tarefas
    xTaskCreate(vTaskDisplay, "Exibir Dados Task", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
    xTaskCreate(vTaskReset, "Reseta Sistema Task", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
    xTaskCreate(vTaskSaida, "Saida Usuario Task", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);
    xTaskCreate(vTaskEntrada, "Entrada Usuario Task", configMINIMAL_STACK_SIZE + 128, NULL, 1, NULL);

    vTaskStartScheduler();
    panic_unsupported();
}

//função mostra cor de led correspondente ao n. de usuários
void atualizar_led() {
    if (usuarios == 0) {
        gpio_put(LED_RED, 0);
        gpio_put(LED_GREEN, 0);
        gpio_put(LED_BLUE, 1); // Azul
    } else if (usuarios < MAX_USERS - 1) {
        gpio_put(LED_RED, 0);
        gpio_put(LED_GREEN, 1);
        gpio_put(LED_BLUE, 0); // Verde
    } else if (usuarios == MAX_USERS - 1) {
        gpio_put(LED_RED, 1);
        gpio_put(LED_GREEN, 1);
        gpio_put(LED_BLUE, 0); // Amarelo
    } else {
        gpio_put(LED_RED, 1);
        gpio_put(LED_GREEN, 0);
        gpio_put(LED_BLUE, 0); // Vermelho
    }
}

//função de callback de interrupção pelos botões
void gpio_irq_handler(uint gpio, uint32_t events) {
    static uint32_t last_time = 0;
    
    //debounce de 200 ms para os botões
    if (to_ms_since_boot(get_absolute_time())-last_time > 200) {
        last_time = to_ms_since_boot(get_absolute_time());
        gpio_callback(gpio, events);
    }
}

//ISR dos botões
void gpio_callback(uint gpio, uint32_t events) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;  //nenhum contexto de tarefa foi despertado

    //verifica qual botão foi pressionado e libera o semáforo correspondente
    switch (gpio)
    {
    case BOTAO_A:
        xSemaphoreGiveFromISR(xAddUserSem, &xHigherPriorityTaskWoken);    //entrada de usuário
        break;
    
    case BOTAO_B:
        xSemaphoreGiveFromISR(xRemoveUserSem, &xHigherPriorityTaskWoken);    //saída de usuário
        break;

    case BOTAO_JOYSTICK:
        xSemaphoreGiveFromISR(xResetUserSem, &xHigherPriorityTaskWoken);    //reset de contagem
        break;
    }
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken); //troca o contexto da tarefa
}

//função para emitir som por 100 ms no buzzer
void beep_curto() {
    pwm_set_gpio_level(BUZZER, 1000);
    vTaskDelay(pdMS_TO_TICKS(100));
    pwm_set_gpio_level(BUZZER, 0);
}

//função para inicializar GPIOs de buzzer (PWM) e botões
void setup_gpios() {
    //BUZZER
    gpio_set_function(BUZZER, GPIO_FUNC_PWM);
    pwm_set_wrap(pwm_gpio_to_slice_num(BUZZER), 1999);
    pwm_set_clkdiv(pwm_gpio_to_slice_num(BUZZER), 125);
    pwm_set_enabled(pwm_gpio_to_slice_num(BUZZER), true);
    pwm_set_gpio_level(BUZZER, 0);

    //Botão A
    gpio_init(BOTAO_A);
    gpio_set_dir(BOTAO_A, GPIO_IN);
    gpio_pull_up(BOTAO_A);

    //Botão B
    gpio_init(BOTAO_B);
    gpio_set_dir(BOTAO_B, GPIO_IN);
    gpio_pull_up(BOTAO_B);

    //Botão Joystick
    gpio_init(BOTAO_JOYSTICK);
    gpio_set_dir(BOTAO_JOYSTICK, GPIO_IN);
    gpio_pull_up(BOTAO_JOYSTICK);
}
#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "lib/Display_Bibliotecas/ssd1306.h"
#include "lib/Matriz_Bibliotecas/matriz_led.h"

/* --------------------------------------------------------------------------- */
/* 1. Mapeamento de hardware                                                   */
/* --------------------------------------------------------------------------- */
#define PINO_BTN_ENTRADA      5     // Botão A
#define PINO_BTN_SAIDA        6     // Botão B
#define PINO_JOYSTICK_RESET   22
#define MAX_USUARIOS          10

/* Display OLED (SSD1306) */
#define I2C_PORT              i2c1
#define I2C_SDA               14
#define I2C_SCL               15
#define OLED_ENDERECO         0x3C
#define OLED_LARGURA          128
#define OLED_ALTURA           64

/* LED RGB (anodos separados) */
#define PINO_LED_VERDE        11
#define PINO_LED_AZUL         12
#define PINO_LED_VERMELHO     13

/* BUZZER piezo */
#define PINO_BUZZER           10

/* --------------------------------------------------------------------------- */
/* 2. Tipos, enuns e tamanhos de filas                                         */
/* --------------------------------------------------------------------------- */
typedef enum {
    CMD_ATUALIZAR_TELA,
    CMD_MOSTRAR_MSG_RESET,
    CMD_OCULTAR_MSG_RESET,
    CMD_ALTERNAR_TELA
} comando_display_t;

#define TAM_FILA_DISPLAY      5

/* --------------------------------------------------------------------------- */
/* 3. Variáveis globais protegidas por mutex                                   */
/* --------------------------------------------------------------------------- */
volatile uint8_t  usuarios_ativos = 0;    // 0-10
volatile uint32_t total_resets    = 0;
volatile bool     mostrar_msg_reset = false;
volatile bool     tela_stats_ativa  = true; // true = Estatísticas, false = Avatares

/* --------------------------------------------------------------------------- */
/* 4.  FreeRTOS (mutexes, semáforos, filas)                                    */
/* --------------------------------------------------------------------------- */
static SemaphoreHandle_t mtx_usuarios;
static SemaphoreHandle_t mtx_oled;
static SemaphoreHandle_t sem_reset_irq;
static SemaphoreHandle_t sem_vagas;        // counting semaphore
static QueueHandle_t     fila_display;

/* --------------------------------------------------------------------------- */
/* 5. Instância do display SSD1306                                             */
/* --------------------------------------------------------------------------- */
static ssd1306_t oled;

/* --------------------------------------------------------------------------- */
/* 6. Utilitário de cor p/ matriz 5×5                                          */
/* --------------------------------------------------------------------------- */
static uint32_t cor_para_numero(uint8_t n)
{
    const uint32_t paleta[] = {
        COR_AZUL, COR_VERDE, COR_LARANJA, COR_VIOLETA, COR_OURO,
        COR_PRATA, COR_MARROM, COR_BRANCO, COR_CINZA, COR_AMARELO
    };
    return (n < 10) ? paleta[n] : COR_VERMELHO;
}

/* --------------------------------------------------------------------------- */
/* 7. Rotina central de desenho + feedback visual                              */
/* --------------------------------------------------------------------------- */
static void desenhar_tela(void)
{
    /* ----- Desenho no OLED -------------------------------------------------- */
    if (xSemaphoreTake(mtx_oled, pdMS_TO_TICKS(100)) == pdTRUE) {
        ssd1306_fill(&oled, false);

        if (tela_stats_ativa) {
            /* TELA 1 – Estatísticas */
            char buf[4][32];
            sprintf(buf[0], "Usuarios: %d/%d", usuarios_ativos, MAX_USUARIOS);

            if      (usuarios_ativos == 0)                 sprintf(buf[1], "Estado: VAZIO");
            else if (usuarios_ativos == MAX_USUARIOS)      sprintf(buf[1], "Estado: LOTADO");
            else if (usuarios_ativos == MAX_USUARIOS-1)    sprintf(buf[1], "Estado: ENCHENDO");
            else                                           sprintf(buf[1], "Estado: NORMAL");

            const char *cor_txt = (usuarios_ativos==0)         ? "AZUL"   :
                                  (usuarios_ativos<=MAX_USUARIOS-2)? "VERDE"  :
                                  (usuarios_ativos==MAX_USUARIOS-1)? "AMARELO": "VERMELHO";
            sprintf(buf[2], "LED: %s", cor_txt);
            sprintf(buf[3], "Resets: %ld", total_resets);

            for (uint8_t i = 0; i < 4; ++i)
                ssd1306_draw_string(&oled, buf[i], 2, 2 + 12*i, false);

            if (mostrar_msg_reset)
                ssd1306_draw_string(&oled, "** RESETADO! **", 15, 52, false);
        } else {
            /* TELA 2 – Avatares */
            const uint8_t L = 12, ESP = 8, P_ROW = 5;
            const uint8_t largura_linha = P_ROW*L + (P_ROW-1)*ESP;
            const int margem_x   = (OLED_LARGURA - largura_linha)/2;
            const int y_superior = (OLED_ALTURA/4)  - L/2;
            const int y_inferior = (OLED_ALTURA*3/4) - L/2;

            for (uint8_t i = 0; i < usuarios_ativos && i < MAX_USUARIOS; ++i) {
                int x = margem_x + (i % P_ROW)*(L+ESP);
                int y = (i < P_ROW) ? y_superior : y_inferior;
                ssd1306_rect(&oled, y, x, L, L, true, true);
            }
        }
        ssd1306_send_data(&oled);
        xSemaphoreGive(mtx_oled);
    }

    /* ----- Feedback LED RGB ------------------------------------------------- */
    bool azul     = (usuarios_ativos == 0);
    bool verde    = (usuarios_ativos > 0 && usuarios_ativos <= MAX_USUARIOS-1);
    bool vermelho = (usuarios_ativos == MAX_USUARIOS);

    gpio_put(PINO_LED_AZUL,     azul);
    gpio_put(PINO_LED_VERDE,    verde);
    gpio_put(PINO_LED_VERMELHO, vermelho);

    /* ----- Feedback matriz 5×5 --------------------------------------------- */
    if (usuarios_ativos == MAX_USUARIOS)
        matriz_draw_pattern(PAD_X, COR_VERMELHO);          // lotado
    else
        matriz_draw_number(usuarios_ativos, cor_para_numero(usuarios_ativos));
}

/* --------------------------------------------------------------------------- */
/* 8. Interrupção do joystick (RESET)                                          */
/* --------------------------------------------------------------------------- */
static volatile uint32_t ultimo_irq_ms = 0;
static const uint32_t   debounce_ms   = 400;

static void irq_joystick(uint gpio, uint32_t eventos)
{
    uint32_t agora = to_ms_since_boot(get_absolute_time());
    if (agora - ultimo_irq_ms < debounce_ms) return;
    ultimo_irq_ms = agora;

    BaseType_t xHigherPriorityTaskWoken = pdFALSE;
    xSemaphoreGiveFromISR(sem_reset_irq, &xHigherPriorityTaskWoken);
    portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
}

/* --------------------------------------------------------------------------- */
/* 9. Tasks FreeRTOS                                                          */
/* --------------------------------------------------------------------------- */

/* Botão A – Entrada --------------------------------------------------------- */
static void task_entrada(void *arg)
{
    bool estado_ant = true;

    while (true) {
        bool estado_atual = gpio_get(PINO_BTN_ENTRADA);

        if (estado_ant && !estado_atual) {
            vTaskDelay(pdMS_TO_TICKS(50));                 // debounce

            if (!gpio_get(PINO_BTN_ENTRADA)) {
                if (xSemaphoreTake(sem_vagas, 0) == pdTRUE) {
                    xSemaphoreTake(mtx_usuarios, portMAX_DELAY);
                    ++usuarios_ativos;
                    xSemaphoreGive(mtx_usuarios);

                    comando_display_t cmd = CMD_ATUALIZAR_TELA;
                    xQueueSendToBack(fila_display, &cmd, 0);
                } else {
                    /* ----- Beep curto: sistema lotado --------------------- */
                    gpio_put(PINO_BUZZER, 1);
                    vTaskDelay(pdMS_TO_TICKS(100));
                    gpio_put(PINO_BUZZER, 0);
                }

                while (!gpio_get(PINO_BTN_ENTRADA))
                    vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        estado_ant = estado_atual;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* Botão B – Saída ----------------------------------------------------------- */
static void task_saida(void *arg)
{
    bool estado_ant = true;

    while (true) {
        bool estado_atual = gpio_get(PINO_BTN_SAIDA);

        if (estado_ant && !estado_atual) {
            vTaskDelay(pdMS_TO_TICKS(50));

            if (!gpio_get(PINO_BTN_SAIDA)) {
                xSemaphoreTake(mtx_usuarios, portMAX_DELAY);

                if (usuarios_ativos > 0) {
                    --usuarios_ativos;
                    xSemaphoreGive(sem_vagas);
                }

                xSemaphoreGive(mtx_usuarios);

                comando_display_t cmd = CMD_ATUALIZAR_TELA;
                xQueueSendToBack(fila_display, &cmd, 0);

                while (!gpio_get(PINO_BTN_SAIDA))
                    vTaskDelay(pdMS_TO_TICKS(10));
            }
        }
        estado_ant = estado_atual;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

/* RESET via joystick -------------------------------------------------------- */
static void task_reset(void *arg)
{
    comando_display_t cmd_show  = CMD_MOSTRAR_MSG_RESET;
    comando_display_t cmd_clear = CMD_OCULTAR_MSG_RESET;

    while (true) {
        if (xSemaphoreTake(sem_reset_irq, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(mtx_usuarios, portMAX_DELAY);

            for (uint8_t i = 0; i < usuarios_ativos; ++i) xSemaphoreGive(sem_vagas);
            usuarios_ativos = 0;
            ++total_resets;

            xSemaphoreGive(mtx_usuarios);

            /* ----- Beep duplo -------------------------------------------- */
            for (uint8_t i = 0; i < 2; ++i) {
                gpio_put(PINO_BUZZER, 1);
                vTaskDelay(pdMS_TO_TICKS(100));
                gpio_put(PINO_BUZZER, 0);
                vTaskDelay(pdMS_TO_TICKS(100));
            }

            xQueueSendToBack(fila_display, &cmd_show, 0);
            vTaskDelay(pdMS_TO_TICKS(2000));
            xQueueSendToBack(fila_display, &cmd_clear, 0);
        }
    }
}

/* Alternar tela ------------------------------------------------------------- */
static void task_alternar_tela(void *arg)
{
    comando_display_t cmd = CMD_ALTERNAR_TELA;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        xQueueSendToBack(fila_display, &cmd, 0);
    }
}

/* Consumidora da fila ------------------------------------------------------- */
static void task_display(void *arg)
{
    comando_display_t cmd;

    while (true) {
        if (xQueueReceive(fila_display, &cmd, portMAX_DELAY) == pdPASS) {
            switch (cmd) {
                case CMD_ATUALIZAR_TELA:        desenhar_tela(); break;
                case CMD_MOSTRAR_MSG_RESET:     mostrar_msg_reset = true;  desenhar_tela(); break;
                case CMD_OCULTAR_MSG_RESET:     mostrar_msg_reset = false; desenhar_tela(); break;
                case CMD_ALTERNAR_TELA:         tela_stats_ativa = !tela_stats_ativa;       desenhar_tela(); break;
            }
        }
    }
}

/* --------------------------------------------------------------------------- */
/* 10. Configuração inicial (main)                                             */
/* --------------------------------------------------------------------------- */
int main(void)
{
    stdio_init_all();

    /* I²C + OLED */
    i2c_init(I2C_PORT, 400000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);  gpio_pull_up(I2C_SCL);

    ssd1306_init(&oled, OLED_LARGURA, OLED_ALTURA, false, OLED_ENDERECO, I2C_PORT);
    ssd1306_config(&oled);

    /* Matriz 5×5 */
    inicializar_matriz_led();

    /* LEDs */
    gpio_init(PINO_LED_VERDE);    gpio_set_dir(PINO_LED_VERDE, GPIO_OUT);
    gpio_init(PINO_LED_AZUL);     gpio_set_dir(PINO_LED_AZUL,  GPIO_OUT);
    gpio_init(PINO_LED_VERMELHO); gpio_set_dir(PINO_LED_VERMELHO, GPIO_OUT);

    /* Buzzer */
    gpio_init(PINO_BUZZER);       gpio_set_dir(PINO_BUZZER, GPIO_OUT);
    gpio_put(PINO_BUZZER, 0);

    /* Botões / Joystick */
    gpio_init(PINO_BTN_ENTRADA);  gpio_set_dir(PINO_BTN_ENTRADA, GPIO_IN); gpio_pull_up(PINO_BTN_ENTRADA);
    gpio_init(PINO_BTN_SAIDA);    gpio_set_dir(PINO_BTN_SAIDA,   GPIO_IN); gpio_pull_up(PINO_BTN_SAIDA);

    gpio_init(PINO_JOYSTICK_RESET);
    gpio_set_dir(PINO_JOYSTICK_RESET, GPIO_IN); gpio_pull_up(PINO_JOYSTICK_RESET);
    gpio_set_irq_enabled_with_callback(PINO_JOYSTICK_RESET, GPIO_IRQ_EDGE_FALL, true, &irq_joystick);

    /* Sincronização */
    mtx_usuarios  = xSemaphoreCreateMutex();
    mtx_oled      = xSemaphoreCreateMutex();
    sem_reset_irq = xSemaphoreCreateBinary();
    sem_vagas     = xSemaphoreCreateCounting(MAX_USUARIOS, MAX_USUARIOS);
    fila_display  = xQueueCreate(TAM_FILA_DISPLAY, sizeof(comando_display_t));

    configASSERT(mtx_usuarios && mtx_oled && sem_reset_irq && sem_vagas && fila_display);

    /* UI inicial */
    desenhar_tela();

    /* Tasks */
    xTaskCreate(task_entrada,        "Entrada",      1024, NULL, 2, NULL);
    xTaskCreate(task_saida,          "Saida",        1024, NULL, 2, NULL);
    xTaskCreate(task_reset,          "Reset",        1024, NULL, 3, NULL);
    xTaskCreate(task_alternar_tela,  "AlternarTela", 1024, NULL, 1, NULL);
    xTaskCreate(task_display,        "Display",      1024, NULL, 2, NULL);

    vTaskStartScheduler();
    while (true);   /* nunca deve chegar aqui */
}

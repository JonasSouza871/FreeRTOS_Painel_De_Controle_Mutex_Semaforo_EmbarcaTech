#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "lib/Display_Bibliotecas/ssd1306.h"

// ===== DEFINIÇÕES =====
#define BOTAO_A_GPIO 5      
#define BOTAO_B_GPIO 6      
#define JOYSTICK_GPIO 22    
#define MAX_USUARIOS 10     

// Configurações I2C e Display
#define I2C_PORT i2c1
#define I2C_SDA 14
#define I2C_SCL 15
#define DISPLAY_ADDRESS 0x3C
#define DISPLAY_WIDTH 128
#define DISPLAY_HEIGHT 64

// ===== VARIÁVEIS GLOBAIS =====
volatile int usuarios_ativos = 0;
SemaphoreHandle_t xMutexUsuarios;
SemaphoreHandle_t xMutexDisplay;
SemaphoreHandle_t xResetSem;
SemaphoreHandle_t xSemaphoreContagem;

// Display
ssd1306_t display;

// Variáveis para debounce da interrupção
volatile uint32_t last_interrupt_time = 0;
const uint32_t debounce_delay_ms = 200;

// ===== FUNÇÃO PARA ATUALIZAR DISPLAY SIMPLIFICADA =====
void atualizar_display() {
    if (xSemaphoreTake(xMutexDisplay, pdMS_TO_TICKS(100)) == pdTRUE) {
        char linha1[32], linha2[32];
        
        // Linha 1: Sempre mostra "Usuarios: x/10"
        sprintf(linha1, "Usuarios: %d/%d", usuarios_ativos, MAX_USUARIOS);
        
        // Linha 2: Estado do sistema
        if (usuarios_ativos == 0) {
            sprintf(linha2, "VAZIO");
        } else if (usuarios_ativos == MAX_USUARIOS) {
            sprintf(linha2, "LOTADO");
        } else if (usuarios_ativos == MAX_USUARIOS - 1) {
            sprintf(linha2, "QUASE CHEIO");
        } else {
            sprintf(linha2, "NORMAL");
        }
        
        ssd1306_fill(&display, false);
        ssd1306_draw_string(&display, linha1, 5, 15, false);
        ssd1306_draw_string(&display, linha2, 5, 35, false);
        ssd1306_send_data(&display);
        
        xSemaphoreGive(xMutexDisplay);
    }
}

// ===== ISR COM DEBOUNCE =====
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    if ((current_time - last_interrupt_time) > debounce_delay_ms) {
        last_interrupt_time = current_time;
        
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}

// ===== TASK BOTÃO A - ENTRADA =====
void vTaskEntrada(void *pvParameters) {
    bool botao_anterior = true;
    bool botao_atual;
    
    while (1) {
        botao_atual = gpio_get(BOTAO_A_GPIO);
        
        if (botao_anterior && !botao_atual) {
            vTaskDelay(pdMS_TO_TICKS(50));
            
            if (!gpio_get(BOTAO_A_GPIO)) {
                if (xSemaphoreTake(xSemaphoreContagem, 0) == pdTRUE) {
                    xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
                    usuarios_ativos++;
                    printf("[ENTRADA] Total: %d/%d\n", usuarios_ativos, MAX_USUARIOS);
                    atualizar_display();
                    xSemaphoreGive(xMutexUsuarios);
                } else {
                    printf("[ENTRADA] LIMITE! %d/%d - BEEP\n", MAX_USUARIOS, MAX_USUARIOS);
                }
                
                while (!gpio_get(BOTAO_A_GPIO)) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
        
        botao_anterior = botao_atual;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ===== TASK BOTÃO B - SAÍDA =====
void vTaskSaida(void *pvParameters) {
    bool botao_anterior = true;
    bool botao_atual;
    
    while (1) {
        botao_atual = gpio_get(BOTAO_B_GPIO);
        
        if (botao_anterior && !botao_atual) {
            vTaskDelay(pdMS_TO_TICKS(50));
            
            if (!gpio_get(BOTAO_B_GPIO)) {
                xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
                
                if (usuarios_ativos > 0) {
                    usuarios_ativos--;
                    xSemaphoreGive(xSemaphoreContagem);
                    printf("[SAIDA] Total: %d/%d\n", usuarios_ativos, MAX_USUARIOS);
                    atualizar_display();
                } else {
                    printf("[SAIDA] Nenhum usuario ativo\n");
                }
                
                xSemaphoreGive(xMutexUsuarios);
                
                while (!gpio_get(BOTAO_B_GPIO)) {
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
            }
        }
        
        botao_anterior = botao_atual;
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// ===== TASK RESET - JOYSTICK COM INTERRUPÇÃO =====
void vTaskReset(void *pvParameters) {
    while (1) {
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
            
            // Restaura todas as vagas no semáforo de contagem
            int vagas_ocupadas = usuarios_ativos;
            for (int i = 0; i < vagas_ocupadas; i++) {
                xSemaphoreGive(xSemaphoreContagem);
            }
            
            usuarios_ativos = 0;
            printf("[RESET] Sistema resetado! Total: %d/%d - BEEP DUPLO\n", usuarios_ativos, MAX_USUARIOS);
            
            atualizar_display();
            
            xSemaphoreGive(xMutexUsuarios);
        }
    }
}

// ===== FUNÇÃO PRINCIPAL =====
int main() {
    stdio_init_all();
    
    // ===== CONFIGURAÇÃO DO I2C E DISPLAY =====
    i2c_init(I2C_PORT, 400 * 1000);
    gpio_set_function(I2C_SDA, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA);
    gpio_pull_up(I2C_SCL);
    
    ssd1306_init(&display, DISPLAY_WIDTH, DISPLAY_HEIGHT, false, DISPLAY_ADDRESS, I2C_PORT);
    ssd1306_config(&display);
    
    // ===== CONFIGURAÇÃO DOS PINOS =====
    gpio_init(BOTAO_A_GPIO);
    gpio_set_dir(BOTAO_A_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_A_GPIO);
    
    gpio_init(BOTAO_B_GPIO);
    gpio_set_dir(BOTAO_B_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_B_GPIO);
    
    gpio_init(JOYSTICK_GPIO);
    gpio_set_dir(JOYSTICK_GPIO, GPIO_IN);
    gpio_pull_up(JOYSTICK_GPIO);
    gpio_set_irq_enabled_with_callback(JOYSTICK_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    
    // ===== CRIAÇÃO DOS SEMÁFOROS =====
    xMutexUsuarios = xSemaphoreCreateMutex();
    xMutexDisplay = xSemaphoreCreateMutex();
    xResetSem = xSemaphoreCreateBinary();
    xSemaphoreContagem = xSemaphoreCreateCounting(MAX_USUARIOS, MAX_USUARIOS);
    
    // ===== EXIBE TELA INICIAL =====
    atualizar_display();
    
    // ===== CRIAÇÃO DAS TASKS =====
    xTaskCreate(vTaskEntrada, "TaskEntrada", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskSaida, "TaskSaida", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskReset, "TaskReset", 1024, NULL, 3, NULL);
    
    // ===== INICIA O SCHEDULER =====
    vTaskStartScheduler();
    
    return 0;
}

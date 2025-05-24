#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"

// ===== DEFINIÇÕES =====
#define BOTAO_A_GPIO 5      
#define BOTAO_B_GPIO 6      
#define JOYSTICK_GPIO 22    
#define MAX_USUARIOS 8      

// ===== VARIÁVEIS GLOBAIS =====
volatile int usuarios_ativos = 0;
SemaphoreHandle_t xMutexUsuarios;
SemaphoreHandle_t xResetSem;
SemaphoreHandle_t xSemaphoreContagem;

// Variáveis para debounce da interrupção
volatile uint32_t last_interrupt_time = 0;
const uint32_t debounce_delay_ms = 200; // 200ms de debounce

// ===== ISR COM DEBOUNCE =====
void gpio_irq_handler(uint gpio, uint32_t events) {
    uint32_t current_time = to_ms_since_boot(get_absolute_time());
    
    // Verifica se passou tempo suficiente desde a última interrupção
    if ((current_time - last_interrupt_time) > debounce_delay_ms) {
        // Atualiza timestamp da última interrupção válida
        last_interrupt_time = current_time;
        
        // Processa a interrupção
        BaseType_t xHigherPriorityTaskWoken = pdFALSE;
        xSemaphoreGiveFromISR(xResetSem, &xHigherPriorityTaskWoken);
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
    // Se não passou tempo suficiente, ignora a interrupção (bounce)
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
                // Tenta adquirir permissão do semáforo de contagem
                if (xSemaphoreTake(xSemaphoreContagem, 0) == pdTRUE) {
                    xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
                    usuarios_ativos++;
                    printf("[ENTRADA] Total: %d/%d\n", usuarios_ativos, MAX_USUARIOS);
                    xSemaphoreGive(xMutexUsuarios);
                } else {
                    // Sistema cheio - emite beep
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
                    // Libera uma vaga no semáforo de contagem
                    xSemaphoreGive(xSemaphoreContagem);
                    printf("[SAIDA] Total: %d/%d\n", usuarios_ativos, MAX_USUARIOS);
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
        // Tarefa bloqueada até que o semáforo seja liberado na ISR
        if (xSemaphoreTake(xResetSem, portMAX_DELAY) == pdTRUE) {
            xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
            
            // Restaura todas as vagas no semáforo de contagem
            int vagas_ocupadas = usuarios_ativos;
            for (int i = 0; i < vagas_ocupadas; i++) {
                xSemaphoreGive(xSemaphoreContagem);
            }
            
            usuarios_ativos = 0;
            printf("[RESET] Sistema resetado! Total: %d/%d - BEEP DUPLO\n", usuarios_ativos, MAX_USUARIOS);
            
            xSemaphoreGive(xMutexUsuarios);
        }
    }
}

// ===== FUNÇÃO PRINCIPAL =====
int main() {
    stdio_init_all();
    
    // ===== CONFIGURAÇÃO DOS PINOS =====
    // Botão A
    gpio_init(BOTAO_A_GPIO);
    gpio_set_dir(BOTAO_A_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_A_GPIO);
    
    // Botão B
    gpio_init(BOTAO_B_GPIO);
    gpio_set_dir(BOTAO_B_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_B_GPIO);
    
    // Joystick com interrupção e debounce
    gpio_init(JOYSTICK_GPIO);
    gpio_set_dir(JOYSTICK_GPIO, GPIO_IN);
    gpio_pull_up(JOYSTICK_GPIO);
    gpio_set_irq_enabled_with_callback(JOYSTICK_GPIO, GPIO_IRQ_EDGE_FALL, true, &gpio_irq_handler);
    
    // ===== CRIAÇÃO DOS SEMÁFOROS =====
    xMutexUsuarios = xSemaphoreCreateMutex();
    xResetSem = xSemaphoreCreateBinary();
    xSemaphoreContagem = xSemaphoreCreateCounting(MAX_USUARIOS, MAX_USUARIOS);
    
    // ===== CRIAÇÃO DAS TASKS =====
    xTaskCreate(vTaskEntrada, "TaskEntrada", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskSaida, "TaskSaida", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskReset, "TaskReset", 1024, NULL, 3, NULL);
    
    // ===== INICIA O SCHEDULER =====
    vTaskStartScheduler();
    
    return 0;
}

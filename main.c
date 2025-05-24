#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"

// ===== DEFINIÇÕES =====
#define BOTAO_A_GPIO 5      
#define BOTAO_B_GPIO 6      
#define MAX_USUARIOS 10

// ===== VARIÁVEIS GLOBAIS =====
volatile int usuarios_ativos = 0;
SemaphoreHandle_t xMutexUsuarios;

// ===== TASK BOTÃO A - ENTRADA =====
void vTaskEntrada(void *pvParameters) {
    bool botao_anterior = true;
    bool botao_atual;
    
    while (1) {
        botao_atual = gpio_get(BOTAO_A_GPIO);
        
        if (botao_anterior && !botao_atual) {
            vTaskDelay(pdMS_TO_TICKS(50));
            
            if (!gpio_get(BOTAO_A_GPIO)) {
                xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
                
                if (usuarios_ativos < MAX_USUARIOS) {
                    usuarios_ativos++;
                    printf("[ENTRADA] Total: %d/%d\n", usuarios_ativos, MAX_USUARIOS);
                } else {
                    printf("[ENTRADA] LIMITE! %d/%d - BEEP\n", usuarios_ativos, MAX_USUARIOS);
                }
                
                xSemaphoreGive(xMutexUsuarios);
                
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

// ===== FUNÇÃO PRINCIPAL =====
int main() {
    stdio_init_all();
    
    // ===== CONFIGURAÇÃO DOS PINOS =====
    gpio_init(BOTAO_A_GPIO);
    gpio_set_dir(BOTAO_A_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_A_GPIO);
    
    gpio_init(BOTAO_B_GPIO);
    gpio_set_dir(BOTAO_B_GPIO, GPIO_IN);
    gpio_pull_up(BOTAO_B_GPIO);
    
    // ===== CRIAÇÃO DO MUTEX =====
    xMutexUsuarios = xSemaphoreCreateMutex();
    
    // ===== CRIAÇÃO DAS TASKS =====
    xTaskCreate(vTaskEntrada, "TaskEntrada", 1024, NULL, 2, NULL);
    xTaskCreate(vTaskSaida, "TaskSaida", 1024, NULL, 2, NULL);
    
    // ===== INICIA O SCHEDULER =====
    vTaskStartScheduler();
    
    return 0;
}

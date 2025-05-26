#include <stdio.h>
#include "pico/stdlib.h"
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "hardware/gpio.h"
#include "hardware/i2c.h"
#include "lib/Display_Bibliotecas/ssd1306.h"
#include "lib/Matriz_Bibliotecas/matriz_led.h"  // Nova biblioteca da matriz LED

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

// LEDs RGB
#define LED_VERDE_GPIO 11   
#define LED_AZUL_GPIO 12    
#define LED_VERMELHO_GPIO 13 

// ===== VARIÁVEIS GLOBAIS =====
volatile int usuarios_ativos = 0;
volatile int contador_resets = 0;
volatile bool mostrar_resetado = false;
volatile bool mostrar_x_vermelho = false;  // Nova flag para X vermelho

SemaphoreHandle_t xMutexUsuarios;
SemaphoreHandle_t xMutexDisplay;
SemaphoreHandle_t xResetSem;
SemaphoreHandle_t xSemaphoreContagem;

// Display
ssd1306_t display;

// Variáveis para debounce da interrupção
volatile uint32_t last_interrupt_time = 0;
const uint32_t debounce_delay_ms = 200;

// ===== FUNÇÃO PARA OBTER COR DA MATRIZ LED BASEADA NO NÚMERO =====
uint32_t obter_cor_numero(int numero) {
    // Cores diferentes para cada número de usuário (0-9)
    const uint32_t cores_numeros[] = {
        COR_AZUL,      // 0 usuários - AZUL
        COR_VERDE,     // 1 usuário - VERDE
        COR_VERDE,     // 2 usuários - VERDE
        COR_VERDE,     // 3 usuários - VERDE
        COR_VERDE,     // 4 usuários - VERDE
        COR_VERDE,     // 5 usuários - VERDE
        COR_VERDE,     // 6 usuários - VERDE
        COR_VERDE,     // 7 usuários - VERDE
        COR_VERDE,     // 8 usuários - VERDE
        COR_AMARELO,   // 9 usuários - AMARELO (quase cheio)
        COR_VERMELHO   // 10 usuários - VERMELHO (lotado)
    };
    
    if (numero >= 0 && numero <= 10) {
        return cores_numeros[numero];
    }
    return COR_VERMELHO;  // Cor padrão para erro
}

// ===== FUNÇÃO PARA ATUALIZAR MATRIZ LED =====
void atualizar_matriz_led() {
    if (mostrar_x_vermelho) {
        // Mostra X vermelho quando tenta exceder limite
        matriz_draw_pattern(PAD_X, COR_VERMELHO);
    } else if (usuarios_ativos <= 9) {
        // Mostra número de usuários com cor correspondente
        uint32_t cor = obter_cor_numero(usuarios_ativos);
        matriz_draw_number(usuarios_ativos, cor);
    } else {
        // Se for 10 usuários, mostra 1 com cor vermelha (representa "10" como "1" vermelho)
        matriz_draw_number(1, COR_VERMELHO);
    }
}

// ===== FUNÇÃO PARA OBTER COR DO LED EM TEXTO =====
const char* obter_cor_led() {
    if (usuarios_ativos == 0) {
        return "AZUL";
    } 
    else if (usuarios_ativos >= 1 && usuarios_ativos <= MAX_USUARIOS - 2) {
        return "VERDE";
    }
    else if (usuarios_ativos == MAX_USUARIOS - 1) {
        return "AMARELO";
    }
    else if (usuarios_ativos == MAX_USUARIOS) {
        return "VERMELHO";
    }
    return "ERRO";
}

// ===== FUNÇÃO PARA CONTROLAR LED RGB =====
void atualizar_led_rgb() {
    if (usuarios_ativos == 0) {
        gpio_put(LED_AZUL_GPIO, 1);
        gpio_put(LED_VERDE_GPIO, 0);
        gpio_put(LED_VERMELHO_GPIO, 0);
    } 
    else if (usuarios_ativos >= 1 && usuarios_ativos <= MAX_USUARIOS - 2) {
        gpio_put(LED_AZUL_GPIO, 0);
        gpio_put(LED_VERDE_GPIO, 1);
        gpio_put(LED_VERMELHO_GPIO, 0);
    }
    else if (usuarios_ativos == MAX_USUARIOS - 1) {
        gpio_put(LED_AZUL_GPIO, 0);
        gpio_put(LED_VERDE_GPIO, 1);    
        gpio_put(LED_VERMELHO_GPIO, 1); 
    }
    else if (usuarios_ativos == MAX_USUARIOS) {
        gpio_put(LED_AZUL_GPIO, 0);
        gpio_put(LED_VERDE_GPIO, 0);
        gpio_put(LED_VERMELHO_GPIO, 1);
    }
}

// ===== FUNÇÃO PARA ATUALIZAR DISPLAY COMPLETA =====
void atualizar_display() {
    if (xSemaphoreTake(xMutexDisplay, pdMS_TO_TICKS(100)) == pdTRUE) {
        char linha1[32], linha2[32], linha3[32], linha4[32], linha5[32];
        
        sprintf(linha1, "Usuarios: %d/%d", usuarios_ativos, MAX_USUARIOS);
        
        if (usuarios_ativos == 0) {
            sprintf(linha2, "Estado: VAZIO");
        } else if (usuarios_ativos == MAX_USUARIOS) {
            sprintf(linha2, "Estado: LOTADO");
        } else if (usuarios_ativos == MAX_USUARIOS - 1) {
            sprintf(linha2, "Estado:ENCHENDO");
        } else {
            sprintf(linha2, "Estado: NORMAL");
        }
        
        sprintf(linha3, "LED: %s", obter_cor_led());
        sprintf(linha4, "Resets: %d", contador_resets);
        
        ssd1306_fill(&display, false);
        ssd1306_draw_string(&display, linha1, 2, 2, false);
        ssd1306_draw_string(&display, linha2, 2, 14, false);  
        ssd1306_draw_string(&display, linha3, 2, 26, false);
        ssd1306_draw_string(&display, linha4, 2, 38, false);
        
        if (mostrar_resetado) {
            sprintf(linha5, "** RESETADO! **");
            ssd1306_draw_string(&display, linha5, 15, 52, false);
        }
        
        ssd1306_send_data(&display);
        xSemaphoreGive(xMutexDisplay);
    }
    
    // Atualiza LED RGB e Matriz LED juntos
    atualizar_led_rgb();
    atualizar_matriz_led();
}

// ===== TASK PARA CONTROLAR X VERMELHO TEMPORÁRIO =====
void vTaskXVermelho(void *pvParameters) {
    while (1) {
        if (mostrar_x_vermelho) {
            vTaskDelay(pdMS_TO_TICKS(2000));  // Espera 2 segundos
            mostrar_x_vermelho = false;
            atualizar_matriz_led();  // Atualiza matriz para voltar ao número
        }
        vTaskDelay(pdMS_TO_TICKS(100));  // Verifica a cada 100ms
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
                    // Sistema cheio - mostra X vermelho por 2 segundos
                    printf("[ENTRADA] LIMITE! %d/%d - BEEP\n", MAX_USUARIOS, MAX_USUARIOS);
                    mostrar_x_vermelho = true;
                    atualizar_matriz_led();
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
            
            int vagas_ocupadas = usuarios_ativos;
            for (int i = 0; i < vagas_ocupadas; i++) {
                xSemaphoreGive(xSemaphoreContagem);
            }
            
            usuarios_ativos = 0;
            contador_resets++;
            
            printf("[RESET] Sistema resetado! Total: %d/%d - BEEP DUPLO (Reset #%d)\n", 
                   usuarios_ativos, MAX_USUARIOS, contador_resets);
            
            mostrar_resetado = true;
            mostrar_x_vermelho = false;  // Remove X vermelho se estiver ativo
            atualizar_display();
            
            xSemaphoreGive(xMutexUsuarios);
            
            vTaskDelay(pdMS_TO_TICKS(2000));
            
            xSemaphoreTake(xMutexUsuarios, portMAX_DELAY);
            mostrar_resetado = false;
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
    
    // ===== CONFIGURAÇÃO DA MATRIZ LED =====
    inicializar_matriz_led();  // Inicializa PIO para WS2812
    printf("Matriz LED 5x5 inicializada!\n");
    
    // ===== CONFIGURAÇÃO DOS LEDS RGB =====
    gpio_init(LED_VERDE_GPIO);
    gpio_set_dir(LED_VERDE_GPIO, GPIO_OUT);
    gpio_put(LED_VERDE_GPIO, 0);
    
    gpio_init(LED_AZUL_GPIO);
    gpio_set_dir(LED_AZUL_GPIO, GPIO_OUT);
    gpio_put(LED_AZUL_GPIO, 0);
    
    gpio_init(LED_VERMELHO_GPIO);
    gpio_set_dir(LED_VERMELHO_GPIO, GPIO_OUT);
    gpio_put(LED_VERMELHO_GPIO, 0);
    
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
    xTaskCreate(vTaskXVermelho, "TaskXVermelho", 512, NULL, 1, NULL);  // Nova task para X vermelho
    
    printf("Sistema completo iniciado com Matriz LED!\n");
    
    // ===== INICIA O SCHEDULER =====
    vTaskStartScheduler();
    
    return 0;
}

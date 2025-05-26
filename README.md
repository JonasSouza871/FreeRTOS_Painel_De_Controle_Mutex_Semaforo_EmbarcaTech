# 🚀 ArquiVaga Pico - Contador de Vagas Interativo
> *Um sistema de contagem de vagas/usuários em tempo real para Raspberry Pi Pico, utilizando FreeRTOS, com display OLED, LEDs RGB, matriz de LEDs 5x5 e buzzer para feedback.*

## 📝 Descrição Breve
Este projeto implementa um sistema de controle e monitoramento de vagas (ou usuários) utilizando o microcontrolador Raspberry Pi Pico. Ele permite o incremento e decremento do número de usuários ativos através de botões, e um botão de joystick para resetar a contagem. O status atual (número de usuários, lotação, total de resets) é exibido em um display OLED SSD1306. Um LED RGB e uma matriz de LEDs 5x5 fornecem feedback visual imediato sobre o estado do sistema (livre, enchendo, lotado), enquanto um buzzer emite alertas sonoros para indicar que o sistema está lotado ou foi resetado. O projeto é construído sobre FreeRTOS para gerenciamento eficiente das tarefas concorrentes (leitura de botões, atualização de display, controle de LEDs e buzzer).

**Componentes principais envolvidos:**
* Raspberry Pi Pico
* Display OLED I2C SSD1306 (128x64)
* Botões Táteis (2 para entrada/saída de usuários)
* Joystick (botão para reset)
* LED RGB (para status geral)
* Matriz de LEDs 5x5 (WS2812 ou similar, para exibir contagem/status)
* Buzzer Passivo (para alertas sonoros)

**Uso esperado ou aplicação prática:**
* Contador de vagas para pequenos estacionamentos ou salas.
* Sistema de controle de acesso simples para eventos ou locais com capacidade limitada.
* Ferramenta educacional para aprender sobre sistemas embarcados, FreeRTOS, e interação com periféricos.

**Tecnologias ou bibliotecas utilizadas:**
* Linguagem: C
* SDK/Framework: Raspberry Pi Pico SDK
* Sistema Operacional: FreeRTOS
* Bibliotecas customizadas para: SSD1306 (Display_Bibliotecas), Matriz de LEDs (Matriz_Bibliotecas)

## ✨ Funcionalidades Principais
* 🔢 **Contagem de Usuários:** Incrementa e decrementa a contagem de usuários ativos.
* 🔘 **Entrada por Botões:** Botões dedicados para registrar entrada e saída de usuários.
* 🕹️ **Reset por Joystick:** Botão do joystick para resetar a contagem de usuários e registrar o total de resets.
* 🖥️ **Display OLED Informativo:** Exibição em tempo real de:
    * Usuários ativos / Capacidade máxima
    * Estado do sistema (VAZIO, NORMAL, ENCHENDO, LOTADO)
    * Cor do LED RGB indicativo
    * Número total de resets
    * Tela alternativa com "avatares" representando usuários ativos.
* 🚥 **Feedback LED RGB:** LED RGB muda de cor para indicar o status:
    * **Azul:** Vazio (0 usuários)
    * **Verde:** Normal/Enchendo (1 a `MAX_USUARIOS - 1` usuários)
    * **Vermelho:** Lotado (`MAX_USUARIOS` usuários)
* 📊 **Visualização em Matriz de LED 5x5:** Exibe o número de usuários ativos com cores correspondentes ou um padrão de "X" quando lotado.
* 🔔 **Alertas Sonoros:** Buzzer para notificar:
    * Sistema lotado ao tentar adicionar novo usuário.
    * Confirmação de reset do sistema.
* 🔄 **Multitarefa com FreeRTOS:** Gerenciamento eficiente de múltiplas operações (leitura de botões, atualização de display, controle de LEDs/buzzer) de forma concorrente.

## ⚙️ Pré-requisitos / Hardware Necessário
### Hardware
| Componente                        | Quant. | Observações                                                              |
| :-------------------------------- | :----: | :----------------------------------------------------------------------- |
| Raspberry Pi Pico                 |   1    | Com headers soldados é recomendado.                                      |
| Display OLED I2C SSD1306 128x64   |   1    | Monocromático. Conexão I2C.                                              |
| Botão Táctil (Push Button)        |   3    | 2 para entrada/saída, 1 no joystick para reset (ou 3 botões avulsos).    |
| Joystick Analógico (opcional)     |   1    | Apenas o botão é usado para reset (PINO_JOYSTICK_RESET). Pode ser um botão comum. |
| LED RGB                           |   1    | Comum Cátodo ou Anodo (ajustar lógica se necessário). O código assume que `gpio_put(PIN, true)` LIGA o LED. |
| Matriz de LEDs 5x5 (ex: WS2812)   |   1    | A biblioteca `matriz_led.h` e `ws2812.pio` sugerem este tipo.            |
| Buzzer Passivo                    |   1    | Controlado por PWM.                                                      |
| Resistores (para LEDs, se nec.)   | Vários | Conforme necessidade dos LEDs, se não integrados em módulos.             |
| Protoboard                        |   1    | Para montagem do circuito.                                               |
| Jumpers Macho-Macho/Macho-Fêmea   | Vários | Para conexões.                                                           |
| Cabo Micro USB                    |   1    | Para alimentação e programação do Pico.                                  |

### Software / Ferramentas
* **Raspberry Pi Pico SDK:** Versão mais recente recomendada.
* **ARM GCC Toolchain:** (e.g., `arm-none-eabi-gcc`)
* **CMake:** Versão 3.13 ou superior.
* **Git:** Para clonar o repositório.
* **IDE (Opcional):** VS Code com extensões C/C++ e CMake Tools.
* **Sistema Operacional Testado:** Linux, macOS, Windows (com WSL2 ou Pico Toolchain).
* **Terminal Serial:** PuTTY, minicom, Tera Term (Baud rate: 115200 bps).

## 🔌 Conexões / Configuração Inicial
### Pinagem resumida (Conforme `main.c`)
| Pino Pico (GP) | Componente          | Função/Conexão                                       |
| :------------- | :------------------ | :--------------------------------------------------- |
| GP5            | Botão Entrada (A)   | Sinal do Botão (Pull-up interno habilitado)          |
| GP6            | Botão Saída (B)     | Sinal do Botão (Pull-up interno habilitado)          |
| GP22           | Botão Reset (Joystick)| Sinal do Botão (Pull-up interno habilitado)        |
| GP14 (I2C1 SDA)| Display OLED        | SDA                                                  |
| GP15 (I2C1 SCL)| Display OLED        | SCL                                                  |
| GP11           | LED RGB             | Pino Verde                                           |
| GP12           | LED RGB             | Pino Azul                                            |
| GP13           | LED RGB             | Pino Vermelho                                        |
| GP10           | Buzzer Passivo      | Sinal PWM para o Buzzer                              |
| GP07 | Matriz de LED 5x5             | Pino de Dados Matriz de led                          |
| 3V3 (OUT)      | Vários              | Alimentação 3.3V para periféricos                    |
| GND            | Vários              | Referência comum de terra para todos os componentes  |

> **Nota Importante:**
> * Certifique-se de que todos os componentes compartilham um **GND comum** com o Raspberry Pi Pico.
> * Verifique a tensão de alimentação correta para cada periférico (principalmente 3.3V do Pico).
> * O pino de dados da Matriz de LEDs (WS2812) é configurado dentro da biblioteca `matriz_led.c` ou através do arquivo `.pio`. Consulte esses arquivos para a pinagem correta.
> * Os botões utilizam pull-up interno do Pico.

### Configuração de Software (primeira vez)
1.  **Clone o repositório:**
    ```bash
    git clone [https://github.com/](https://github.com/)[SEU_USUARIO]/[Nome-do-Seu-Projeto-RTOS_Pico_Contador].git
    cd [Nome-do-Seu-Projeto-RTOS_Pico_Contador]
    ```

2.  **Inicialize e atualize os submódulos (se o Pico SDK estiver como submódulo):**
    ```bash
    git submodule update --init --recursive
    ```
    Caso contrário, certifique-se de que o `PICO_SDK_PATH` esteja configurado.

3.  **Verifique as configurações no `main.c`:**
    * `MAX_USUARIOS` (Padrão: 10)
    * Pinos de hardware (se diferentes do padrão definido)

## ▶️ Como Compilar e Executar
Siga estes passos para compilar o projeto:

1.  **Crie e acesse o diretório de build:**
    A partir da raiz do projeto:
    ```bash
    mkdir build
    cd build
    ```

2.  **Configure o CMake apontando para o Pico SDK (se não estiver globalmente configurado ou como submódulo na raiz):**
    Exemplo:
    ```bash
    export PICO_SDK_PATH=/caminho/absoluto/para/pico-sdk
    # ou, se relativo ao diretório de build:
    # export PICO_SDK_PATH=../../pico-sdk
    cmake ..
    ```
    Se o SDK estiver em um local padrão ou for encontrado automaticamente (ex: via `pico_sdk_import.cmake` e `git submodule`), apenas `cmake ..` pode ser suficiente.

3.  **Compile o projeto:**
    ```bash
    make -j$(nproc)  # Para Linux/macOS
    # ou
    # make -jX       # Onde X é o número de núcleos
    ```
    Isso gerará um arquivo `.uf2` (e.g., `nome_do_projeto.uf2`) dentro do diretório `build`.

**Para gravar na placa (Raspberry Pi Pico):**
1.  Desconecte o Pico da alimentação (USB).
2.  Pressione e mantenha pressionado o botão **BOOTSEL** no Pico.
3.  Conecte o Pico ao seu computador via cabo USB enquanto mantém o BOOTSEL pressionado.
4.  O Pico aparecerá como um dispositivo de armazenamento em massa.
5.  Arraste e solte o arquivo `.uf2` gerado para dentro do dispositivo de armazenamento do Pico.
6.  O Pico irá reiniciar automaticamente e começar a executar o firmware.

**Como acessar logs/interfaces:**
* **Logs (Serial):**
    * Conecte-se ao Pico usando um programa de terminal serial (PuTTY, minicom, Tera Term, etc.).
    * Configure a porta serial correspondente ao Pico e use uma taxa de transmissão (baud rate) de **115200 bps**.
    * Mensagens de inicialização (se houver `printf` no código) e outros logs de depuração podem ser visualizados aqui. (Seu `main.c` atual usa `stdio_init_all()` mas não parece ter `printf` para logs de rotina, apenas para a configuração inicial).

## 🤝 Contribuições (Opcional)
Pull requests são bem-vindos. Para mudanças maiores, por favor, abra uma issue primeiro para discutir o que você gostaria de mudar.

## 👤 Autor / Contato
* **Nome:** Jonas Souza
* **E-mail:** Jonassouza871@hotmail.com

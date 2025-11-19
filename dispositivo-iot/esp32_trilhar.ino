/*
 * GS Edge Computing: Timer Pomodoro "Trilhar"
 * * FUNCIONALIDADES:
 * 1. Timer Configurável (Foco e Pausa) via MQTT.
 * 2. Lógica de 4 Ciclos (3 pausas curtas -> 1 pausa longa).
 * 3. Pausa Automática com Sensor Ultrassônico (HC-SR04).
 * 4. Feedback sonoro com Buzzer.
 * 5. Contador de Ciclos Totais (Ouro).
 *
 * Rodrigo Cardoso Tadeo - RM: 562010
 * Vinicius Cavalcanti dos Reis - RM: 562063
 */

// --- BIBLIOTECAS ---
#include <WiFi.h>         // Para conectar ao WiFi do Wokwi
#include <PubSubClient.h> // Para comunicação MQTT

// --- CONFIGURAÇÃO DE REDE ---
const char* ssid = "Wokwi-GUEST"; // Rede padrão do simulador
const char* password = "";
// !!! IMPORTANTE: SUBSTITUA PELO IP DA SUA VM AZURE !!!
const char* MQTT_BROKER_IP = "20.81.162.205"; 
const int MQTT_PORT = 1883; 

// --- TÓPICOS MQTT (Definidos no IoT Agent) ---
const char* API_KEY = "TEF";
const char* DEVICE_ID = "pomodoro001";
char MQTT_TOPIC_PUBLISH[100];   // Onde o ESP32 escreve status
char MQTT_TOPIC_SUBSCRIBE[100]; // Onde o ESP32 lê comandos

// --- DEFINIÇÃO DOS PINOS DO HARDWARE ---
const int BTN_START_PIN = 12;   // Botão Verde
const int BTN_PAUSE_PIN = 13;   // Botão Amarelo
const int BTN_RESTART_PIN = 14; // Botão Vermelho
const int BUZZER_PIN = 15;      // Buzzer Piezo
const int SENSOR_TRIG_PIN = 26; // Sensor Ultrassônico (Gatilho)
const int SENSOR_ECHO_PIN = 25; // Sensor Ultrassônico (Eco)

// --- MÁQUINA DE ESTADOS FINITOS ---
// Define os possíveis estados do sistema para controle lógico
enum State { 
  OCIOSO,           // Esperando iniciar
  FOCO,             // Contando tempo de estudo
  PAUSA_FOCO,       // Estudo pausado (manual ou sensor)
  FOCO_CONCLUIDO,   // Estudo acabou, esperando usuário iniciar descanso
  PAUSA_DESCANSO    // Contando tempo de descanso
};
State currentState = OCIOSO;

// --- VARIÁVEIS GLOBAIS ---
int duracaoConfiguradaMin = 25;       // Tempo de foco (padrão)
int duracaoPausaConfiguradaMin = 5;   // Tempo de pausa curta (padrão)
const int DURACAO_PAUSA_LONGA_MIN = 20; // Pausa longa
String proximaPausaTipo = "curta";    // Informa o dashboard se é curta ou longa

unsigned long tempoRestanteSeg = 0;   // Contador regressivo principal
int sessoesCompletas = 0;             // Conta sessões parciais (0 a 4)
int ciclosTotais = 0;                 // Conta ciclos completos de 4 sessões

// --- CONTROLE DE TEMPO (Não Bloqueante) ---
unsigned long previousMillis = 0; 
const long interval = 1000; // 1 segundo
unsigned long previousPublishMillis = 0; 
const long publishInterval = 1000; // Publica a cada 1s

// --- CONTROLE DO SENSOR ---
unsigned long lastPresenceTime = 0; 
const long absenceThreshold = 30000; // 30s de ausência dispara pausa

// Clientes WiFi e MQTT
WiFiClient espClient;
PubSubClient client(espClient);

// --- PROTÓTIPOS DE FUNÇÃO (Para evitar erros de compilação) ---
void playBuzzer(int type);
void publishMQTT(String overrideStatus); // Removemos o valor padrão aqui

// =========================================================
// 1. SETUP (Executado uma vez ao iniciar)
// =========================================================
void setup() {
  Serial.begin(115200); 
  Serial.println("--- INICIANDO SISTEMA POMODORO TRILHAR ---");

  // Configura modo dos pinos
  pinMode(BTN_START_PIN, INPUT_PULLUP);
  pinMode(BTN_PAUSE_PIN, INPUT_PULLUP);
  pinMode(BTN_RESTART_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(SENSOR_TRIG_PIN, OUTPUT);
  pinMode(SENSOR_ECHO_PIN, INPUT);

  // Monta as strings dos tópicos MQTT
  snprintf(MQTT_TOPIC_PUBLISH, 100, "/%s/%s/attrs", API_KEY, DEVICE_ID);
  snprintf(MQTT_TOPIC_SUBSCRIBE, 100, "/%s/%s/cmd", API_KEY, DEVICE_ID);

  setup_wifi(); // Conecta ao WiFi
  
  // Configura o servidor MQTT e a função de callback (receber mensagens)
  client.setServer(MQTT_BROKER_IP, MQTT_PORT);
  client.setCallback(mqttCallback);

  Serial.println("Sistema pronto e aguardando comandos.");
}

// Conecta ao WiFi do Wokwi
void setup_wifi() { 
  delay(10); Serial.print("Conectando WiFi: "); Serial.println(ssid);
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) { delay(500); Serial.print("."); }
  Serial.println("\nConectado!");
}

// Gerencia a reconexão ao MQTT se cair
void reconnect() { 
  while (!client.connected()) {
    Serial.print("Tentando MQTT...");
    // Conecta com um ID único
    if (client.connect("ESP32_Pomodoro_Controlador_Final")) {
      Serial.println("Conectado!");
      // Assina o tópico para receber comandos do Dashboard
      client.subscribe(MQTT_TOPIC_SUBSCRIBE);
      // Envia o estado inicial
      publishMQTT(""); 
    } else {
      Serial.print("Falha, rc="); Serial.print(client.state()); 
      Serial.println(" Tentando em 5s...");
      delay(5000);
    }
  }
}

// =========================================================
// 2. MQTT CALLBACK (Onde os comandos do Dashboard chegam)
// =========================================================
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (int i = 0; i < length; i++) { message += (char)payload[i]; }
  Serial.print("CMD Recebido: "); Serial.println(message);

  // Parse da mensagem (ex: "pomodoro001@set_duracao|15")
  int atIndex = message.indexOf('@'); int barIndex = message.indexOf('|');
  if (atIndex == -1 || barIndex == -1) return;
  
  String commandName = message.substring(atIndex + 1, barIndex);
  String commandValue = message.substring(barIndex + 1);

  // Só aceita configurações se estiver OCIOSO
  if (currentState == OCIOSO) { 
    if (commandName == "set_duracao") {
      duracaoConfiguradaMin = commandValue.toInt();
      // MODO TESTE: Valor recebido é tratado como SEGUNDOS
      Serial.println("Configuração atualizada: Tempo de Foco");
      playBuzzer(1); publishMQTT(""); 
    } else if (commandName == "set_pausa") { 
      duracaoPausaConfiguradaMin = commandValue.toInt();
      Serial.println("Configuração atualizada: Tempo de Pausa");
      playBuzzer(1); publishMQTT(""); 
    }
  }
}

// =========================================================
// 3. LOOP PRINCIPAL (Executado continuamente)
// =========================================================
void loop() {
  if (WiFi.status() != WL_CONNECTED) { setup_wifi(); }
  if (!client.connected()) { reconnect(); }
  client.loop(); // Mantém o MQTT vivo
  
  checkPresenceSensor(); // 1. Verifica se o usuário está na mesa
  handleButtons();       // 2. Verifica se algum botão foi apertado  
  handleTimer();         // 3. Atualiza o cronômetro
}

// Função auxiliar para ler o sensor ultrassônico
long readDistance() { 
  digitalWrite(SENSOR_TRIG_PIN, LOW); delayMicroseconds(2);
  digitalWrite(SENSOR_TRIG_PIN, HIGH); delayMicroseconds(10);
  digitalWrite(SENSOR_TRIG_PIN, LOW);
  long duration = pulseIn(SENSOR_ECHO_PIN, HIGH);
  return (duration / 2) * 0.0343; // Converte para cm
}

// Lógica da Pausa Automática (Sensor)
void checkPresenceSensor() {
  // Só monitora se estiver em FOCO
  if (currentState != FOCO) { lastPresenceTime = millis(); return; }
  
  long distancia = readDistance();
  // Se distância > 50cm ou 0 (erro), considera ausente
  if (distancia > 50 || distancia == 0) {
    // Se ausente por mais que o limite (30s)
    if (millis() - lastPresenceTime > absenceThreshold) {
      Serial.println("PAUSA AUTOMÁTICA: Usuário ausente.");
      currentState = PAUSA_FOCO; // Pausa o timer
      playBuzzer(1);
      publishMQTT("pausa_automatica"); // Envia status especial
      lastPresenceTime = millis(); 
    }
  } else { lastPresenceTime = millis(); } // Reset se presente
}

// Leitura dos botões físicos
void handleButtons() { 
  if (digitalRead(BTN_START_PIN) == LOW) { startTimer(); }
  if (digitalRead(BTN_PAUSE_PIN) == LOW) { pauseTimer(); }
  if (digitalRead(BTN_RESTART_PIN) == LOW) { restartTimer(); }
  delay(200); // Debounce simples
}

// Lógica Principal do Cronômetro
void handleTimer() {
  // O timer só roda se estiver em FOCO ou DESCANSANDO
  if (currentState != FOCO && currentState != PAUSA_DESCANSO) return;
  
  unsigned long currentMillis = millis();

  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis; 
    if (tempoRestanteSeg > 0) tempoRestanteSeg--; 

    // Publica telemetria periodicamente
    if (currentMillis - previousPublishMillis >= publishInterval) {
      previousPublishMillis = currentMillis;
      publishMQTT(""); 
    }

    // Quando o tempo acaba
    if (tempoRestanteSeg <= 0) {
      if (currentState == FOCO) {
        // --- FIM DO FOCO ---
        currentState = FOCO_CONCLUIDO;
        sessoesCompletas++; 
        
        // Regra dos 4 Ciclos
        if (sessoesCompletas == 4) {
          // Pausa longa
          tempoRestanteSeg = DURACAO_PAUSA_LONGA_MIN * 60; 
          proximaPausaTipo = "longa"; 
          Serial.println("FIM CICLO 4. Pausa Longa preparada.");
        } else {
          // Pausa curta
          tempoRestanteSeg = duracaoPausaConfiguradaMin * 60; 
          proximaPausaTipo = "curta";
          Serial.println("Fim do Foco. Pausa Curta preparada.");
        }
        playBuzzer(3); 
        publishMQTT(""); 
      } 
      else if (currentState == PAUSA_DESCANSO) { 
        // --- FIM DA PAUSA ---
        currentState = OCIOSO; 
        tempoRestanteSeg = 0; 
        playBuzzer(2); 
        
        if (sessoesCompletas == 4) {
            // Completou o ciclo de ouro (4x4)
            ciclosTotais++; 
            sessoesCompletas = 0; // Reseta parciais
            proximaPausaTipo = "curta"; 
            Serial.println("FIM PAUSA LONGA. Ciclo Total +1.");
        } else {
            if ((sessoesCompletas + 1) == 4) proximaPausaTipo = "longa";
            Serial.println("FIM DA PAUSA.");
        }
        publishMQTT("");
      }
    }
  }
}

// =========================================================
// 4. AÇÕES DE CONTROLE
// =========================================================

void startTimer() {
  if (currentState == OCIOSO) {
    Serial.println("INICIANDO FOCO.");
    currentState = FOCO;
    tempoRestanteSeg = duracaoConfiguradaMin * 60; 
    lastPresenceTime = millis(); 
    playBuzzer(1);
    publishMQTT("");
  }
}

void pauseTimer() {
  if (currentState == FOCO) {
    currentState = PAUSA_FOCO; playBuzzer(1); publishMQTT(""); // Pausa Manual
  } else if (currentState == PAUSA_FOCO) {
    currentState = FOCO; lastPresenceTime = millis(); playBuzzer(1); publishMQTT(""); // Retoma
  } else if (currentState == FOCO_CONCLUIDO) {
    currentState = PAUSA_DESCANSO; playBuzzer(1); publishMQTT(""); // Inicia Descanso
  }
}

void restartTimer() { 
  Serial.println("RESTART TOTAL.");
  currentState = OCIOSO;
  tempoRestanteSeg = 0;
  sessoesCompletas = 0; // Zera sessões parciais
  // Não zera ciclosTotais para persistir a conquista
  proximaPausaTipo = "curta"; 
  playBuzzer(2);
  publishMQTT("");
}

// Controle do Buzzer (Feedback Sonoro)
void playBuzzer(int type) {
  if (type == 1) { tone(BUZZER_PIN, 800, 200); } // Bipe Curto
  else if (type == 2) { tone(BUZZER_PIN, 800, 200); delay(250); tone(BUZZER_PIN, 800, 200); } // Bipe Duplo
  else if (type == 3) { tone(BUZZER_PIN, 1000, 300); delay(350); tone(BUZZER_PIN, 1000, 300); delay(350); tone(BUZZER_PIN, 1000, 300); } // Alarme
}

// =========================================================
// 5. INTEGRAÇÃO MQTT (Envio de Dados)
// =========================================================
void publishMQTT(String overrideStatus) {
  if (!client.connected()) return; 
  
  String statusStr;
  if (overrideStatus != "") statusStr = overrideStatus; 
  else {
    if (currentState == FOCO) statusStr = "foco"; 
    else if (currentState == PAUSA_FOCO) statusStr = "pausa_foco"; 
    else if (currentState == PAUSA_DESCANSO) statusStr = "pausa_descanso";
    else if (currentState == FOCO_CONCLUIDO) statusStr = "foco_concluido";
    else statusStr = "ocioso";
  }

  // Payload UL2.0 com TODOS os atributos, incluindo Ciclos Totais (cct)
  char payload[150]; 
  snprintf(payload, 150, "st|%s|sc|%d|dc|%d|tr|%lu|dpc|%d|ppt|%s|cct|%d",
           statusStr.c_str(), 
           sessoesCompletas, 
           duracaoConfiguradaMin, 
           tempoRestanteSeg,
           duracaoPausaConfiguradaMin,
           proximaPausaTipo.c_str(),
           ciclosTotais); 

  Serial.print("PUB: "); Serial.println(payload);
  client.publish(MQTT_TOPIC_PUBLISH, payload);
}
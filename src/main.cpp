#include <Arduino.h>
#include <DHT.h>
#include <Wire.h>

// --- DEFINIÇÃO DE PINOS E PERIFÉRICOS (Baseado no Hardware da AMoVA) ---
const int PINO_DHT22 = 4;          // Pino de dados do sensor DHT22
const int PINO_SENSOR_LDR = 22;    // Exemplo de pino I2C para BH1750 (SCL/SDA usam pinos fixos 22/21)
const int PINO_MOSFET_LED = 23;    // Pino PWM que controla o Farol de 48W via MOSFET IRLZ44N
const int PINO_SPEAKER_I2S = 25;   // Pino de dados para o amplificador MAX98357 (I2S)
const int PINO_BATERIA = 34;       // Pino analógico (ADC) para monitorar a tensão da LiFePO4 (com divisor de tensão!)

// Inicialização do sensor DHT22
DHT dht(PINO_DHT22, DHT22);

void setup() {
  // Inicializa a comunicação Serial para monitoramento no computador
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("==========================================");
  Serial.println(" Iniciando Estacao AMoVA - Prototipo ");
  Serial.println("==========================================");

  // Inicializa sensores e atuadores
  dht.begin();
  Wire.begin(); // Inicializa barramento I2C para o BH1750 e outros módulos
  
  pinMode(PINO_MOSFET_LED, OUTPUT);
  digitalWrite(PINO_MOSFET_LED, LOW); // Garante que o farol inicia apagado
  
  // Configuração básica do canal PWM para o LED (Dimerização)
  // Nota: No ESP32 com frameworks recentes, usamos ledcAttach e ledcWrite
  ledcAttach(PINO_MOSFET_LED, 5000, 8); // Canal para o pino, frequência 5kHz, resolução de 8 bits
}

void loop() {
  // --- 1. LEITURA DOS SENSORES AMBIENTAIS ---
  float umidade = dht.readHumidity();
  float temperatura = dht.readTemperature();

  if (isnan(umidade) || isnan(temperatura)) {
    Serial.println("Erro ao ler dados do sensor DHT22!");
  } else {
    Serial.print("Temperatura: ");
    Serial.print(temperatura);
    Serial.print(" °C | Umidade: ");
    Serial.print(umidade);
    Serial.println(" %");
  }

  // --- 2. MONITORAMENTO DA BATERIA (LiFePO4) ---
  int leituraBruta = analogRead(PINO_BATERIA);
  // Conversão aproximada considerando divisor de tensão adequado para 12V -> 3.3V
  float tensaoBateria = (leituraBruta / 4095.0) * 3.3 * 4.0; 
  
  Serial.print("Tensao da Bateria: ");
  Serial.print(tensaoBateria);
  Serial.println(" V");

  // Estratégia de economia de energia básica: se a tensão cair muito, desativa cargas
  if (tensaoBateria < 11.0 && tensaoBateria > 2.0) {
    Serial.println("[ALERTA] Bateria baixa! Entrando em modo de economia de energia.");
    // Desliga o farol por segurança
    ledcWrite(PINO_MOSFET_LED, 0);
  }

  // --- 3. TESTE DE ESTÍMULO (Simulação de Experimento) ---
  // Exemplo: Acende o farol de milha a 50% da potência por 3 segundos
  Serial.println("Acionando iluminação experimental (50% de intensidade)...");
  ledcWrite(PINO_MOSFET_LED, 128); // 128 de 255 (50% PWM)
  delay(3000);

  Serial.println("Desligando iluminação.");
  ledcWrite(PINO_MOSFET_LED, 0);   // Apaga o farol
  
  Serial.println("------------------------------------------");
  delay(10000); // Aguarda 10 segundos antes do próximo ciclo
}
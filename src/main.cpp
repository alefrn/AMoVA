#include <Arduino.h>
#include <Wire.h>
#include <SPI.h>
#include <SD.h>
#include <DHT.h>
#include <BH1750.h>

// --- DEFINIÇÃO DE PINOS (Sem conflitos SPI/I2C) ---
const int PINO_DHT22       = 4;   // Dados DHT22
const int PINO_SD_CS       = 5;   // Chip Select MicroSD (SCK=18, MISO=19, MOSI=23)
const int PINO_AUDIO_SIM   = 25;  // Saída de áudio (simulação Wokwi / futuro I2S MAX98357)
const int PINO_MOSFET_LED  = 26;  // PWM do Farol 48W (IRLZ44N)
const int PINO_BATERIA     = 34;  // ADC Bateria LiFePO4 (divisor 100k / 10k)

// --- OBJETOS DOS SENSORES ---
DHT dht(PINO_DHT22, DHT22);
BH1750 sensorLuz;

// --- PARÂMETROS DO EXPERIMENTO E ENERGIA ---
const float TENSAO_CRITICA_V = 11.0; // Limite para modo economia de energia
const float FATOR_DIVISOR    = 11.0; // (100k + 10k) / 10k = 11
const char* ARQUIVO_LOG      = "/log_amova.csv";

// Janela Experimental (18:00 às 06:00)
const int HORA_INICIO_EXP = 18;
const int HORA_FIM_EXP    = 6;

// --- CONTROLE DE TEMPO E ALEATORIEDADE ---
unsigned long ultimoCicloLog       = 0;
unsigned long inicioFaseAtual      = 0;
unsigned long duracaoFaseAtual     = 5000; // Tempo sorteado para o estado atual
const unsigned long INTERVALO_LOG  = 3000; // Grava dados a cada 3s para acompanhar o relógio

// --- ESTADOS DA ESTAÇÃO ---
enum EstadoOperacao {
  MONITORAMENTO,
  ESTIMULO_LUMINOSO,
  ESTIMULO_SONORO,
  ESTIMULO_COMBINADO,
  ECONOMIA_ENERGIA
};

EstadoOperacao estadoAtual = MONITORAMENTO;
bool sdDisponivel          = false;
bool bh1750Disponivel      = false;
int potenciaLedAtual       = 0;

// --- FUNÇÕES AUXILIARES ---
float lerTensaoBateria() {
  int leituraADC = analogRead(PINO_BATERIA);
  return (leituraADC / 4095.0) * 3.3 * FATOR_DIVISOR;
}

// Simula um relógio de 24h acelerado (1s real = 10 min virtuais), iniciando às 17:30
void obterHoraSimulada(unsigned long ms, int &hora, int &minuto) {
  unsigned long minutosTotais = (17 * 60 + 30) + (ms / 100);
  hora   = (minutosTotais / 60) % 24;
  minuto = minutosTotais % 60;
}

String formatarHora(int hora, int minuto) {
  String h = (hora < 10) ? "0" + String(hora) : String(hora);
  String m = (minuto < 10) ? "0" + String(minuto) : String(minuto);
  return h + ":" + m;
}

bool dentroJanelaExperimental(int hora) {
  if (HORA_INICIO_EXP > HORA_FIM_EXP) {
    // Janela que vira a noite (ex: 18h às 06h)
    return (hora >= HORA_INICIO_EXP || hora < HORA_FIM_EXP);
  }
  return (hora >= HORA_INICIO_EXP && hora < HORA_FIM_EXP);
}

String nomeEstado(EstadoOperacao est) {
  switch (est) {
    case MONITORAMENTO:      return "MONITORAMENTO";
    case ESTIMULO_LUMINOSO:  return "ESTIMULO_LUZ";
    case ESTIMULO_SONORO:    return "ESTIMULO_SOM";
    case ESTIMULO_COMBINADO: return "ESTIMULO_COMBINADO";
    case ECONOMIA_ENERGIA:   return "ECONOMIA_ENERGIA";
    default:                 return "DESCONHECIDO";
  }
}

void registrarDadosSD(String linha) {
  if (!sdDisponivel) return;
  File arquivo = SD.open(ARQUIVO_LOG, FILE_APPEND);
  if (arquivo) {
    arquivo.println(linha);
    arquivo.close();
  }
}

void aplicarAtuadores(EstadoOperacao est, float luxAtual) {
  int pwm = 0;

  switch (est) {
    case MONITORAMENTO:
    case ECONOMIA_ENERGIA:
      pwm = 0;
      noTone(PINO_AUDIO_SIM);
      break;

    case ESTIMULO_LUMINOSO:
      if (luxAtual < 20.0) pwm = 102;       // 40% (noite)
      else if (luxAtual <= 100.0) pwm = 191;// 75% (crepúsculo)
      else pwm = 255;                       // 100% (dia)
      noTone(PINO_AUDIO_SIM);
      break;

    case ESTIMULO_SONORO:
      pwm = 0;
      tone(PINO_AUDIO_SIM, 440);
      break;

    case ESTIMULO_COMBINADO:
      pwm = (luxAtual < 50.0) ? 128 : 220;
      tone(PINO_AUDIO_SIM, 600);
      break;
  }

  ledcWrite(PINO_MOSFET_LED, pwm);
  potenciaLedAtual = map(pwm, 0, 255, 0, 100);
}

void sortearProximoEvento(unsigned long agora) {
  inicioFaseAtual = agora;

  if (estadoAtual == MONITORAMENTO) {
    // Estava em pausa -> Sorteia um dos 3 estímulos (1=Luz, 2=Som, 3=Combinado)
    estadoAtual = (EstadoOperacao)random(1, 4);
    duracaoFaseAtual = random(5000, 12001); // Duração entre 5s e 12s no simulador
    Serial.println(">>> [INICIO EVENTO] " + nomeEstado(estadoAtual) + 
                   " | Duracao sorteada: " + String(duracaoFaseAtual / 1000) + "s");
  } else {
    // Estava aplicando estímulo -> Volta para pausa de monitoramento
    Serial.println("<<< [FIM EVENTO] Encerrando " + nomeEstado(estadoAtual));
    estadoAtual = MONITORAMENTO;
    duracaoFaseAtual = random(4000, 10001); // Intervalo sem estímulo entre 4s e 10s
    Serial.println("    Pausa aleatoria de monitoramento: " + String(duracaoFaseAtual / 1000) + "s");
  }
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESTACAO AMoVA - AGENDADOR ALEATORIO E JANELAS ===");

  // Inicializa gerador de números aleatórios com ruído analógico
  randomSeed(analogRead(0));

  ledcAttach(PINO_MOSFET_LED, 5000, 8);
  ledcWrite(PINO_MOSFET_LED, 0);
  pinMode(PINO_AUDIO_SIM, OUTPUT);

  dht.begin();
  Wire.begin();
  if (sensorLuz.begin()) {
    bh1750Disponivel = true;
  }

  if (SD.begin(PINO_SD_CS)) {
    sdDisponivel = true;
    Serial.println("[OK] Cartao MicroSD iniciado.");
    if (!SD.exists(ARQUIVO_LOG)) {
      registrarDadosSD("Tempo_ms;Hora_Sim;Temp_C;Umid_%;Lux;Bateria_V;Estado;Potencia_LED_%");
    }
  }
}

void loop() {
  unsigned long agora = millis();
  int horaSim, minSim;
  obterHoraSimulada(agora, horaSim, minSim);
  bool janelaAtiva = dentroJanelaExperimental(horaSim);

  // 1. Prioridade Máxima: Proteção de Bateria
  float tensaoBat = lerTensaoBateria();
  if (tensaoBat < TENSAO_CRITICA_V && tensaoBat > 1.0) {
    if (estadoAtual != ECONOMIA_ENERGIA) {
      Serial.println("[ALERTA] Bateria critica! Suspendendo experimentos.");
      estadoAtual = ECONOMIA_ENERGIA;
    }
  } else if (estadoAtual == ECONOMIA_ENERGIA && tensaoBat >= (TENSAO_CRITICA_V + 0.5)) {
    Serial.println("[INFO] Bateria recuperada.");
    estadoAtual = MONITORAMENTO;
    inicioFaseAtual = agora;
    duracaoFaseAtual = 3000;
  }

  // 2. Controle da Janela Experimental e Sorteio de Eventos
  if (estadoAtual != ECONOMIA_ENERGIA) {
    if (!janelaAtiva) {
      // Fora do horário experimental: força MONITORAMENTO sem estímulos
      if (estadoAtual != MONITORAMENTO) {
        Serial.println("[JANELA ENCERRADA] Fora do horario experimental. Voltando ao MONITORAMENTO.");
        estadoAtual = MONITORAMENTO;
      }
    } else {
      // Dentro do horário experimental: verifica se o tempo sorteado acabou
      if (agora - inicioFaseAtual >= duracaoFaseAtual) {
        sortearProximoEvento(agora);
      }
    }
  }

  // 3. Leitura de sensores, atuação e log CSV
  if (agora - ultimoCicloLog >= INTERVALO_LOG) {
    ultimoCicloLog = agora;

    float temp = dht.readTemperature();
    float umid = dht.readHumidity();
    float lux  = bh1750Disponivel ? sensorLuz.readLightLevel() : (janelaAtiva ? 15.0 : 250.0);

    aplicarAtuadores(estadoAtual, lux);

    String tempStr = isnan(temp) ? "ERRO" : String(temp, 1);
    String umidStr = isnan(umid) ? "ERRO" : String(umid, 1);
    String linhaCSV = String(agora) + ";" + formatarHora(horaSim, minSim) + ";" +
                      tempStr + ";" + umidStr + ";" + String(lux, 1) + ";" +
                      String(tensaoBat, 2) + ";" + nomeEstado(estadoAtual) + ";" +
                      String(potenciaLedAtual) + "%";

    Serial.println("LOG: " + linhaCSV);
    registrarDadosSD(linhaCSV);
  }
}
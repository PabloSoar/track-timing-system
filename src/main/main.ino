#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>
#include <LittleFS.h>
#include <DNSServer.h>

DNSServer dnsServer;
const char *ssid = "Cronometro_Lidar";
const char *password = "cronometro123"; // Minimo de 8 caracteres para WPA2
const int largura_raia = 122;

WebServer server(80);
HardwareSerial tfLunaSerial(2);

// Comandos hexadecimais para gestão de energia do TF-Luna
uint8_t ligar_sensor[]    = {0x5A, 0x05, 0x07, 0x01, 0x00};
uint8_t desligar_sensor[] = {0x5A, 0x05, 0x07, 0x00, 0x00};

// Variáveis de tempo e estado
int estado = 0; // 0 = Aguardando, 1 = Single-lane, 2 = Multi-Lane
unsigned long long offsetTempo = 0; 
unsigned long long tempoLargada = 0;
unsigned long long tempoChegada = 0;
float tempoTotalSegundos = 0;

// Variáveis do Modo Multi-Lane
int numeroRaias = 1;
int raiasFinalizadas = 0;
float temposRaias[6]; // Suporta até 6 raias

unsigned long long getTempoSincronizado() {
  return millis() + offsetTempo;
}

void serveIndex() {
  File f = LittleFS.open("/index.html", "r");
  if (!f) {
    server.send(500, "text/plain", "index.html nao encontrado");
    return;
  }

  server.streamFile(f, "text/html");
  f.close();
}

void setup() {
  Serial.begin(115200);
  tfLunaSerial.begin(115200, SERIAL_8N1, 16, 17);

  // 1. Sobe o Access Point PRIMEIRO
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(192, 168, 4, 1);
  IPAddress subnet(255, 255, 255, 0);
  WiFi.mode(WIFI_AP);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  WiFi.softAP(ssid, password);

  // 2. Sobe o DNS logo após o AP
  dnsServer.start(53, "*", local_IP);
  Serial.print("AP no IP: ");
  Serial.println(WiFi.softAPIP());

  // 3. Monta o LittleFS
  if (!LittleFS.begin()) {
    Serial.println("Falha ao montar LittleFS!");
  }

  // 4. Registra as rotas
  server.on("/", serveIndex);

// Rota de Sincronização
  server.on("/sync", []() {
    if (server.hasArg("t")) {
      unsigned long long tempoCelular = strtoull(server.arg("t").c_str(), NULL, 10);
      offsetTempo = tempoCelular - millis();
      Serial.println("Relógios Sincronizados!");
      server.send(200, "text/plain", "OK");
    }
  });

  // Rota para ler o Status em tempo real
  server.on("/status", []() {
    String json = "{\"estado\":" + String(estado) + ", \"tempos\":[";
    for(int i=0; i<numeroRaias; i++) {
      json += String(temposRaias[i], 3);
      if(i < numeroRaias - 1) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  // Rota de Largada
  server.on("/start", []() {
    String reqMode = server.arg("mode");
    
    // Configura o estado baseado no HTML
    if (reqMode == "single") {
      estado = 1;
      numeroRaias = 1;
    } else {
      estado = 2;
      if (server.hasArg("lanes")) numeroRaias = server.arg("lanes").toInt();
    }
    
    if(numeroRaias < 1) numeroRaias = 1;
    if(numeroRaias > 6) numeroRaias = 6;
    
    // Pega o tempo de largada exato do celular para ignorar a latência do Wi-Fi
    if (server.hasArg("t")) {
      tempoLargada = strtoull(server.arg("t").c_str(), NULL, 10);
    } else {
      tempoLargada = getTempoSincronizado(); // Prevenção de falhas
    }

    // Zera tudo para a nova corrida
    raiasFinalizadas = 0;
    for(int i=0; i<6; i++) temposRaias[i] = 0.0;

    // Acorda o sensor LIDAR
    tfLunaSerial.write(ligar_sensor, 5);
    delay(50);
    while (tfLunaSerial.available()) tfLunaSerial.read(); // Limpa o buffer

    Serial.println("Largada registrada! Modo: " + reqMode);
    server.send(200, "text/plain", "OK");
  });

  // 5. Captive portal por último, antes do server.begin()
  // O DNS wildcard faz qualquer hostname apontar para o ESP32. Estas rotas
  // cobrem as checagens comuns de Android, iOS/macOS e Windows.
  server.on("/generate_204", serveIndex);
  server.on("/gen_204", serveIndex);
  server.on("/hotspot-detect.html", serveIndex);
  server.on("/library/test/success.html", serveIndex);
  server.on("/ncsi.txt", serveIndex);
  server.on("/connecttest.txt", serveIndex);
  server.on("/redirect", serveIndex);
  server.onNotFound(serveIndex);

  server.begin();

  // 6. Configura o sensor
  uint8_t cmd_250Hz[] = {0x5A, 0x06, 0x03, 0xFA, 0x00, 0x00};
  tfLunaSerial.write(cmd_250Hz, 6);
  delay(100);
  uint8_t cmd_save[] = {0x5A, 0x04, 0x11, 0x00};
  tfLunaSerial.write(cmd_save, 4);
  delay(100);
  tfLunaSerial.write(desligar_sensor, 5);
  Serial.println("Pronto!");
}

void loop() {
  dnsServer.processNextRequest();
  server.handleClient();

  if (tfLunaSerial.available() >= 9) {
    if (tfLunaSerial.read() == 0x59) {
      if (tfLunaSerial.read() == 0x59) {
        
        int dist_L = tfLunaSerial.read();
        int dist_H = tfLunaSerial.read();
        int amp_L = tfLunaSerial.read();
        int amp_H = tfLunaSerial.read();
        tfLunaSerial.read(); tfLunaSerial.read(); tfLunaSerial.read(); 
        
        int distance = dist_L + (dist_H << 8);
        int strength = amp_L + (amp_H << 8);

        Serial.println(distance);

        if (estado == 1){
          if (strength > 100 && distance > 20 && distance < 200) {
            
            tempoChegada = getTempoSincronizado();
            
            // Força a matemática a aceitar negativos para evitar o "ovf"
            long long diferenca = (long long)tempoChegada - (long long)tempoLargada;

            tempoTotalSegundos = diferenca / 1000.0;

            if (tempoTotalSegundos > 0.5){
              temposRaias[0] = tempoTotalSegundos;
              estado = 0; // Finaliza
              
              tfLunaSerial.write(desligar_sensor, 5);
              Serial.print("Distância gatilho: ");
              Serial.println(distance);
              Serial.print("Tempo final calculado: ");
              Serial.println(tempoTotalSegundos, 3);
          }
        }
      }
        if (estado == 2) {
          if (strength > 100 && distance > 5 && distance < largura_raia * numeroRaias) {
              int indiceRaia = (distance / largura_raia); 
              if (temposRaias[indiceRaia] == 0.0) {
                
                tempoChegada = getTempoSincronizado();
              
                // Força a matemática a aceitar negativos para evitar o "ovf"
                long long diferenca = (long long)tempoChegada - (long long)tempoLargada;
                
                tempoTotalSegundos = diferenca / 1000.0;

                if (tempoTotalSegundos > 0.5){
                  temposRaias[indiceRaia] = tempoTotalSegundos;
                  raiasFinalizadas++;
                  Serial.print("Raia "); Serial.print(indiceRaia + 1);
                  Serial.print(" cruzou! (Distância: "); Serial.print(distance);
                  Serial.print("cm) - Tempo: "); Serial.println(temposRaias[indiceRaia], 3);
                }
                
                if (raiasFinalizadas >= numeroRaias) {
                  estado = 0; // Finaliza
                  tfLunaSerial.write(desligar_sensor, 5);
                  Serial.println("Todas as raias finalizaram. Sensor DESLIGADO.");
                }
              }
            }
          }
        }
      }
    }
  }

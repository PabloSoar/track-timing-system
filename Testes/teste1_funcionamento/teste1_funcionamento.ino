#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>

const char *ssid = "Cronometro_Lidar";
const char *password = "atletismo123";

WebServer server(80);
HardwareSerial tfLunaSerial(2);

// Variáveis de tempo usando unsigned long long para caber o timestamp Unix (em milissegundos)
int estado = 0; // 0 = Aguardando, 1 = Correndo, 2 = Finalizado
unsigned long long offsetTempo = 0; 
unsigned long long tempoLargada = 0;
unsigned long long tempoChegada = 0;
float tempoTotalSegundos = 0.0;

// Função para pegar o tempo absoluto sincronizado com o celular
unsigned long long getTempoSincronizado() {
  return millis() + offsetTempo;
}

// ==============================================================================
// INTERFACE WEB (HTML + JS)
// ==============================================================================
const char* html_page = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Cronômetro Lidar</title>
  <style>
    body { font-family: Arial; text-align: center; background: #222; color: #fff; margin-top: 50px; }
    button { padding: 20px 40px; font-size: 24px; color: white; background: #d9534f; border: none; border-radius: 10px; cursor: pointer; }
    #display { font-size: 60px; font-family: monospace; margin: 30px 0; color: #5cb85c; }
    #status { font-size: 20px; color: #f0ad4e; }
  </style>
</head>
<body>
  <h1>Cronômetro de Pista</h1>
  <div id="status">Sincronizando relógios...</div>
  <div id="display">0.000 s</div>
  <button id="btnLargada" onclick="iniciarCorrida()" disabled>📢 LARGADA</button>

  <script>
    let intervaloPoll;

    // Sincroniza os relógios assim que a página carrega
    window.onload = function() {
      const tempoAtualCelular = Date.now();
      fetch(`/sync?t=${tempoAtualCelular}`)
        .then(() => {
          document.getElementById('status').innerText = "Relógios Sincronizados. Aguardando...";
          document.getElementById('btnLargada').disabled = false;
        });
    };

    function tocarBeep() {
      const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
      const osc = audioCtx.createOscillator();
      osc.type = 'square';
      osc.frequency.setValueAtTime(800, audioCtx.currentTime);
      osc.connect(audioCtx.destination);
      osc.start();
      osc.stop(audioCtx.currentTime + 0.3);
    }

    function iniciarCorrida() {
      tocarBeep();
      
      // Pega o tempo EXATO da largada no relógio do celular
      const tLargada = Date.now(); 
      
      document.getElementById('display').innerText = "Correndo...";
      document.getElementById('status').innerText = "Aguardando cruzamento da linha...";
      
      // Envia o tempo exato da largada para o ESP32
      fetch(`/start?t=${tLargada}`).then(() => {
        intervaloPoll = setInterval(checarStatus, 200);
      });
    }

    function checarStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          if (data.estado === 2) { 
            clearInterval(intervaloPoll);
            document.getElementById('display').innerText = data.tempo.toFixed(3) + " s";
            document.getElementById('status').innerText = "Tempo Oficial!";
            document.getElementById('btnLargada').innerText = "NOVA CORRIDA";
          }
        });
    }
  </script>
</body>
</html>
)rawliteral";
// ==============================================================================

void setup() {
  Serial.begin(115200);
  tfLunaSerial.begin(115200, SERIAL_8N1, 16, 17);
  
  WiFi.softAP(ssid, password);
  Serial.print("Servidor no IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() {
    server.send(200, "text/html", html_page);
  });

  // ROTA 1: Sincronização dos Relógios
  server.on("/sync", []() {
    if (server.hasArg("t")) {
      unsigned long long tempoCelular = strtoull(server.arg("t").c_str(), NULL, 10);
      offsetTempo = tempoCelular - millis(); // Calcula a diferença
      Serial.println("Relógios sincronizados!");
      server.send(200, "text/plain", "OK");
    }
  });

  // ROTA 2: Recebe o Timestamp da Largada
  server.on("/start", []() {
    if (server.hasArg("t")) {
      tempoLargada = strtoull(server.arg("t").c_str(), NULL, 10);
      
      // O SEGREDO 1: Esvazia qualquer dado velho acumulado do sensor
      while (tfLunaSerial.available()) {
        tfLunaSerial.read();
      }

      estado = 1; 
      Serial.println("Largada recebida e buffer limpo!");
      server.send(200, "text/plain", "OK");
    }
  });

  // ROTA 3: Celular pergunta se a corrida acabou
  server.on("/status", []() {
    String json = "{\"estado\":" + String(estado) + ", \"tempo\":" + String(tempoTotalSegundos, 3) + "}";
    server.send(200, "application/json", json);
  });

  server.begin();

  // 1. Array com o comando para alterar a frequência para 250Hz
  uint8_t cmd_250Hz[] = {0x5A, 0x06, 0x03, 0xFA, 0x00, 0x00};
  tfLunaSerial.write(cmd_250Hz, 6); // Envia os 6 bytes
  delay(100); // Pausa breve para o sensor processar

  // 2. Array com o comando para SALVAR a configuração permanentemente
  uint8_t cmd_save[] = {0x5A, 0x04, 0x11, 0x00};
  tfLunaSerial.write(cmd_save, 4); // Envia os 4 bytes
  delay(100);
  
  Serial.println("TF-Luna configurado para 250Hz e salvo!");
}

void loop() {
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

        if (estado == 1) {
          if (strength > 100 && distance > 5 && distance < 50) {
            
            tempoChegada = getTempoSincronizado();
            
            // Força a matemática a aceitar negativos para evitar o "ovf"
            long long diferenca = (long long)tempoChegada - (long long)tempoLargada;
            
            // Proteção contra latência extrema
            if (diferenca < 0) {
                diferenca = 0; 
            }
            
            tempoTotalSegundos = diferenca / 1000.0;
            estado = 2; // Finaliza
            
            Serial.print("Distância gatilho: ");
            Serial.println(distance);
            Serial.print("Tempo final calculado: ");
            Serial.println(tempoTotalSegundos, 3);
          }
        }
      }
    }
  }
}
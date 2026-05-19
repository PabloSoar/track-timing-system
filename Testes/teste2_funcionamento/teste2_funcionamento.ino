#include <WiFi.h>
#include <WebServer.h>
#include <HardwareSerial.h>

const char *ssid = "Cronometro_Lidar";
const char *password = "atletismo123";

WebServer server(80);
HardwareSerial tfLunaSerial(2);

// Comandos hexadecimais para gestão de energia do TF-Luna
uint8_t ligar_sensor[]    = {0x5A, 0x05, 0x07, 0x01, 0x00};
uint8_t desligar_sensor[] = {0x5A, 0x05, 0x07, 0x00, 0x00};

// Variáveis de tempo e estado
int estado = 0; // 0 = Aguardando, 1 = Correndo, 2 = Finalizado
unsigned long long offsetTempo = 0; 
unsigned long long tempoLargada = 0;
unsigned long long tempoChegada = 0;

// Variáveis do Modo Multi-Lane
int numeroRaias = 1;
int raiasFinalizadas = 0;
float temposRaias[8]; // Suporta até 8 raias

unsigned long long getTempoSincronizado() {
  return millis() + offsetTempo;
}

// ==============================================================================
// CAPTIVE PORTAL: Redireciona para a pagina principal do cronometro
// ==============================================================================
void handleCaptivePortal() {
  IPAddress apIP = WiFi.softAPIP();
  String redirectURL = "http://" + apIP.toString() + "/";

  Serial.print("Captive Portal detectado: ");
  Serial.println(server.uri());

  server.sendHeader("Location", redirectURL, true);
  server.send(302, "text/plain", "Redirecionando...");
}

// ==============================================================================
// INTERFACE WEB (HTML + CSS + JS)
// ==============================================================================
const char* html_page = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-BR">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1.0">
  <title>Cronômetro Lidar</title>
  <style>
    body { font-family: Arial; text-align: center; background: #222; color: #fff; margin: 30px auto; max-width: 600px; padding: 0 15px;}
    button { padding: 15px 30px; font-size: 20px; color: white; background: #d9534f; border: none; border-radius: 10px; cursor: pointer; width: 100%; margin: 15px 0;}
    button:disabled { background: #777; cursor: not-allowed; }
    select { padding: 10px; font-size: 18px; border-radius: 5px; margin-bottom: 15px; width: 100%;}
    #display { font-size: 28px; font-family: monospace; margin: 20px 0; color: #5cb85c; line-height: 1.4;}
    #status { font-size: 18px; color: #f0ad4e; margin-bottom: 20px;}
    .history-container { margin-top: 30px; text-align: left; background: #333; padding: 15px; border-radius: 10px; }
    .history-container h3 { margin-top: 0; color: #5bc0de; }
    #historico { list-style: none; padding: 0; margin: 0;}
    #historico li { border-bottom: 1px solid #444; padding: 8px 0; font-family: monospace;}
  </style>
</head>
<body>
  <h1>Cronômetro de Pista</h1>
  
  <select id="selRaias">
    <option value="1">Modo Padrão (1 Raia)</option>
    <option value="2">Multi-Lane (2 Raias)</option>
    <option value="3">Multi-Lane (3 Raias)</option>
    <option value="4">Multi-Lane (4 Raias)</option>
    <option value="5">Multi-Lane (5 Raias)</option>
    <option value="6">Multi-Lane (6 Raias)</option>
    <option value="8">Multi-Lane (8 Raias)</option>
  </select>

  <div id="status">Sincronizando relógios...</div>
  <button id="btnLargada" onclick="iniciarCorrida()" disabled>📢 LARGADA</button>
  <div id="display">0.000 s</div>

  <div class="history-container">
    <h3>Histórico de Tempos</h3>
    <ul id="historico"></ul>
  </div>

  <script>
    let intervaloPoll;
    let contadorCorridas = 1;

    window.onload = function() {
      const tempoAtualCelular = Date.now();
      fetch(`/sync?t=${tempoAtualCelular}`)
        .then(() => {
          document.getElementById('status').innerText = "Pronto para largada!";
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
      const tLargada = Date.now(); 
      const qtdRaias = document.getElementById('selRaias').value;
      
      document.getElementById('btnLargada').disabled = true;
      document.getElementById('display').innerHTML = "Correndo...";
      document.getElementById('status').innerText = "Aguardando cruzamentos...";
      
      fetch(`/start?t=${tLargada}&raias=${qtdRaias}`).then(() => {
        intervaloPoll = setInterval(checarStatus, 200);
      });
    }

    function checarStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          let displayHtml = "";
          data.tempos.forEach((t, i) => {
            const tempoStr = t > 0 ? t.toFixed(3) + " s" : "---";
            displayHtml += `<div>Raia ${i+1}: <span style="color:#fff">${tempoStr}</span></div>`;
          });
          document.getElementById('display').innerHTML = displayHtml;

          if (data.estado === 2) { 
            clearInterval(intervaloPoll);
            document.getElementById('status').innerText = "Prova Encerrada!";
            document.getElementById('btnLargada').disabled = false;
            document.getElementById('btnLargada').innerText = "NOVA LARGADA";
            
            const linhaHist = `<b>Corrida ${contadorCorridas}</b><br>` + 
              data.tempos.map((t, i) => `R${i+1}: ${t.toFixed(3)}s`).join(' | ');
            
            document.getElementById('historico').innerHTML += `<li>${linhaHist}</li>`;
            contadorCorridas++;
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
  
  // CAPTIVE PORTAL
  server.on("/generate_204", HTTP_GET, handleCaptivePortal);
  server.on("/gen_204", HTTP_GET, handleCaptivePortal);
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
  server.on("/captive.apple.com", HTTP_GET, handleCaptivePortal);
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
  server.on("/detectportal.firefox.com", HTTP_GET, handleCaptivePortal);
  server.on("/canonical.html", HTTP_GET, handleCaptivePortal);
  server.onNotFound(handleCaptivePortal);

  // GATEWAY
  IPAddress local_IP(192, 168, 4, 1);
  IPAddress gateway(0, 0, 0, 0); 
  IPAddress subnet(255, 255, 255, 0);
  WiFi.softAPConfig(local_IP, gateway, subnet);
  
  WiFi.softAP(ssid, password);
  Serial.print("Servidor no IP: ");
  Serial.println(WiFi.softAPIP());

  server.on("/", []() {
    server.send(200, "text/html", html_page);
  });

  server.on("/sync", []() {
    if (server.hasArg("t")) {
      unsigned long long tempoCelular = strtoull(server.arg("t").c_str(), NULL, 10);
      offsetTempo = tempoCelular - millis();
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/start", []() {
    if (server.hasArg("t") && server.hasArg("raias")) {
      tempoLargada = strtoull(server.arg("t").c_str(), NULL, 10);
      numeroRaias = server.arg("raias").toInt();
      
      if(numeroRaias < 1) numeroRaias = 1;
      if(numeroRaias > 8) numeroRaias = 8;
      
      raiasFinalizadas = 0;
      for(int i=0; i<8; i++) temposRaias[i] = 0.0;

      // 1. Acorda o LIDAR
      tfLunaSerial.write(ligar_sensor, 5);
      
      // 2. CORREÇÃO: Dá 50ms para o hardware do sensor ligar fisicamente e enviar o "lixo" inicial
      delay(50);
      
      // 3. Esvazia buffer sujo
      while (tfLunaSerial.available()) {
        tfLunaSerial.read();
      }

      estado = 1; 
      Serial.println("Largada recebida! Sensor LIGADO.");
      server.send(200, "text/plain", "OK");
    }
  });

  server.on("/status", []() {
    String json = "{\"estado\":" + String(estado) + ", \"tempos\":[";
    for(int i=0; i<numeroRaias; i++) {
      json += String(temposRaias[i], 3);
      if(i < numeroRaias - 1) json += ",";
    }
    json += "]}";
    server.send(200, "application/json", json);
  });

  server.begin();

  uint8_t cmd_250Hz[] = {0x5A, 0x06, 0x03, 0xFA, 0x00, 0x00};
  tfLunaSerial.write(cmd_250Hz, 6); 
  delay(100); 

  uint8_t cmd_save[] = {0x5A, 0x04, 0x11, 0x00};
  tfLunaSerial.write(cmd_save, 4); 
  delay(100);
  
  tfLunaSerial.write(desligar_sensor, 5);
  Serial.println("TF-Luna configurado para 250Hz e aguardando em modo DORMIR!");
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
          
          long long tempoDecorrido = (long long)getTempoSincronizado() - (long long)tempoLargada;
          
          // "Janela Cega" de 500ms. Ignora falsos disparos no exato momento da largada
          if (tempoDecorrido > 500) {
            
            if (strength > 100 && distance > 5) {
              
              int indiceRaia = (distance / 10); 
              
              if (indiceRaia < numeroRaias && temposRaias[indiceRaia] == 0.0) {
                
                tempoChegada = getTempoSincronizado();
                long long diferenca = (long long)tempoChegada - (long long)tempoLargada;
                if (diferenca < 0) diferenca = 0; 
                
                temposRaias[indiceRaia] = diferenca / 1000.0;
                raiasFinalizadas++;
                
                Serial.print("Raia "); Serial.print(indiceRaia + 1);
                Serial.print(" cruzou! (Distância: "); Serial.print(distance);
                Serial.print("cm) - Tempo: "); Serial.println(temposRaias[indiceRaia], 3);
                
                if (raiasFinalizadas >= numeroRaias) {
                  estado = 2; // Finaliza
                  
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
}

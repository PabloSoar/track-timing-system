#include <WiFi.h>
#include <WebServer.h>
#include <DNSServer.h>
#include <HardwareSerial.h>

// ==============================================================================
// CONFIGURACAO: Escolha o modo de operacao
// ==============================================================================
#define USAR_SENSOR_LIDAR false  // true = usar TF-Luna, false = simular com botao

const char *ssid = "Cronometro_Lidar";
const char *password = "atletismo123";

WebServer server(80);
DNSServer dnsServer;

#if USAR_SENSOR_LIDAR
HardwareSerial tfLunaSerial(2);
#endif

// ==============================================================================
// PINOS
// ==============================================================================
const int PINO_BOTAO = 32;  // Botao para simular sensor ( usar com INPUT_PULLUP )

// ==============================================================================
// VARIAVEIS DE ESTADO E TEMPO
// ==============================================================================
int estado = 0; // 0 = Aguardando, 1 = Correndo, 2 = Finalizado

unsigned long long offsetTempo = 0;
unsigned long long tempoLargada = 0;
float tempoTotalSegundos = 0.0;

// ==============================================================================
// VARIAVEIS DE VOLTAS
// ==============================================================================
const int MAX_VOLTAS = 20;
int numVoltasTotal = 1;                    // Numero total de voltas (configuravel pela web)
int voltasCompletadas = 0;                 // Voltas ja completadas
unsigned long long temposVoltas[MAX_VOLTAS]; // Tempos de cada volta em milissegundos (desde a largada)

// ==============================================================================
// DEBOUNCE DO BOTAO (modo simulacao)
// ==============================================================================
unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;   // ms
int ultimoEstadoBotao = HIGH;
int estadoBotao = HIGH;

// ==============================================================================
// FUNCOES AUXILIARES
// ==============================================================================
unsigned long long getTempoSincronizado() {
  return millis() + offsetTempo;
}

String montarJsonStatus() {
  String json = "{";
  json += "\"estado\":" + String(estado);
  json += ",\"voltasCompletadas\":" + String(voltasCompletadas);
  json += ",\"numVoltasTotal\":" + String(numVoltasTotal);
  json += ",\"tempoTotal\":" + String(tempoTotalSegundos, 3);
  json += ",\"temposVoltas\":[";
  for (int i = 0; i < voltasCompletadas; i++) {
    json += String(temposVoltas[i] / 1000.0, 3);
    if (i < voltasCompletadas - 1) json += ",";
  }
  json += "]}";
  return json;
}

// ==============================================================================
// CAPTIVE PORTAL: Redireciona para a pagina principal do cronometro
// ==============================================================================
void handleCaptivePortal() {
  IPAddress apIP = WiFi.softAPIP();
  String redirectURL = "http://" + apIP.toString() + "/";

  // Log para debug no Serial
  Serial.print("Captive Portal detectado: ");
  Serial.println(server.uri());

  server.sendHeader("Location", redirectURL, true);
  server.send(302, "text/plain", "Redirecionando...");
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
  <title>Cronometro de Pista - Voltas</title>
  <style>
    body { font-family: Arial; text-align: center; background: #222; color: #fff; margin-top: 30px; padding: 0 10px; }
    h1 { font-size: 28px; }
    .config { margin: 20px 0; font-size: 18px; }
    .config input { width: 60px; font-size: 18px; text-align: center; padding: 5px; border-radius: 5px; border: none; }
    button { padding: 15px 30px; font-size: 22px; color: white; background: #d9534f; border: none; border-radius: 10px; cursor: pointer; margin: 10px; }
    button:disabled { background: #666; cursor: not-allowed; }
    #display { font-size: 50px; font-family: monospace; margin: 20px 0; color: #5cb85c; }
    #volta-atual { font-size: 22px; color: #f0ad4e; margin: 10px 0; }
    #status { font-size: 18px; color: #ccc; margin: 10px 0; }
    #tabela-voltas { margin: 20px auto; border-collapse: collapse; width: 90%; max-width: 400px; display: none; }
    #tabela-voltas th, #tabela-voltas td { border: 1px solid #555; padding: 8px; text-align: center; }
    #tabela-voltas th { background: #333; }
    #tabela-voltas td { background: #444; }
    .tempo-volta { font-size: 16px; color: #5cb85c; margin: 5px 0; }
  </style>
</head>
<body>
  <h1>Cronometro de Pista</h1>

  <div class="config">
    <label for="numVoltas">Numero de Voltas:</label>
    <input type="number" id="numVoltas" value="1" min="1" max="20">
  </div>

  <div id="status">Sincronizando relogios...</div>
  <div id="volta-atual"></div>
  <div id="display">0.000 s</div>

  <button id="btnLargada" onclick="iniciarCorrida()" disabled>LARGADA</button>
  <button id="btnReset" onclick="resetarCorrida()" disabled>NOVA CORRIDA</button>

  <div id="tempos-vivo"></div>

  <table id="tabela-voltas">
    <thead>
      <tr><th>Volta</th><th>Tempo Parcial (s)</th><th>Tempo da Volta (s)</th></tr>
    </thead>
    <tbody id="corpo-tabela"></tbody>
  </table>

  <script>
    let intervaloPoll;
    let intervaloDisplay;
    let numVoltasConfigurado = 1;
    let tLargadaLocal = 0;
    let estadoCorrida = 0; // 0=aguardando, 1=correndo, 2=finalizado
    let ultimasVoltasExibidas = 0;

    window.onload = function() {
      const tempoAtualCelular = Date.now();
      fetch(`/sync?t=${tempoAtualCelular}`)
        .then(() => {
          document.getElementById('status').innerText = "Relogios Sincronizados. Aguardando...";
          document.getElementById('btnLargada').disabled = false;
        });
    };

    function tocarBeep(freq, duracao) {
      try {
        const audioCtx = new (window.AudioContext || window.webkitAudioContext)();
        const osc = audioCtx.createOscillator();
        osc.type = 'square';
        osc.frequency.setValueAtTime(freq, audioCtx.currentTime);
        osc.connect(audioCtx.destination);
        osc.start();
        osc.stop(audioCtx.currentTime + duracao);
      } catch(e) {}
    }

    function iniciarCorrida() {
      const inputVoltas = document.getElementById('numVoltas');
      numVoltasConfigurado = parseInt(inputVoltas.value) || 1;
      if (numVoltasConfigurado < 1) numVoltasConfigurado = 1;
      if (numVoltasConfigurado > 20) numVoltasConfigurado = 20;

      // Desabilita controles durante a contagem regressiva
      document.getElementById('btnLargada').disabled = true;
      document.getElementById('numVoltas').disabled = true;
      document.getElementById('btnReset').disabled = true;
      document.getElementById('tabela-voltas').style.display = 'none';
      document.getElementById('corpo-tabela').innerHTML = "";
      document.getElementById('tempos-vivo').innerHTML = "";
      ultimasVoltasExibidas = 0;

      let contador = 3;
      document.getElementById('status').innerText = "Prepare-se...";
      document.getElementById('display').innerText = contador;
      tocarBeep(600, 0.2); // beep inicial

      const countdownInterval = setInterval(() => {
        contador--;
        if (contador > 0) {
          document.getElementById('display').innerText = contador;
          tocarBeep(600, 0.2); // beep a cada segundo
        } else if (contador === 0) {
          clearInterval(countdownInterval);
          document.getElementById('display').innerText = "GO!";
          document.getElementById('status').innerText = "Correndo...";
          document.getElementById('volta-atual').innerText = "Volta 0 / " + numVoltasConfigurado;
          tocarBeep(1200, 0.5); // beep final mais longo e agudo

          // Dispara a largada oficial
          tLargadaLocal = Date.now();
          estadoCorrida = 1;
          intervaloDisplay = setInterval(atualizarDisplayVivo, 50);
          fetch(`/start?t=${tLargadaLocal}&voltas=${numVoltasConfigurado}`).then(() => {
            intervaloPoll = setInterval(checarStatus, 200);
          });
        }
      }, 1000);
    }

    function atualizarDisplayVivo() {
      if (estadoCorrida === 1) {
        const decorrido = (Date.now() - tLargadaLocal) / 1000.0;
        document.getElementById('display').innerText = decorrido.toFixed(3) + " s";
      }
    }

    function resetarCorrida() {
      fetch('/reset').then(() => {
        clearInterval(intervaloPoll);
        clearInterval(intervaloDisplay);
        estadoCorrida = 0;
        ultimasVoltasExibidas = 0;
        document.getElementById('display').innerText = "0.000 s";
        document.getElementById('status').innerText = "Relogios Sincronizados. Aguardando...";
        document.getElementById('volta-atual').innerText = "";
        document.getElementById('tempos-vivo').innerHTML = "";
        document.getElementById('tabela-voltas').style.display = 'none';
        document.getElementById('corpo-tabela').innerHTML = "";
        document.getElementById('btnLargada').disabled = false;
        document.getElementById('btnReset').disabled = true;
        document.getElementById('numVoltas').disabled = false;
      });
    }

    function checarStatus() {
      fetch('/status')
        .then(response => response.json())
        .then(data => {
          estadoCorrida = data.estado;
          const vComp = data.voltasCompletadas;
          const vTotal = data.numVoltasTotal;

          document.getElementById('volta-atual').innerText =
            "Volta " + vComp + " / " + vTotal;

          // Se completou uma nova volta, mostra notificacao
          if (vComp > ultimasVoltasExibidas) {
            ultimasVoltasExibidas = vComp;
            tocarBeep(1200, 0.15);
            atualizarTabela(data);
          }

          if (data.estado === 2) {
            clearInterval(intervaloPoll);
            clearInterval(intervaloDisplay);
            document.getElementById('display').innerText = data.tempoTotal.toFixed(3) + " s";
            document.getElementById('status').innerText = "Corrida Finalizada!";
            document.getElementById('volta-atual').innerText =
              "Total: " + vTotal + " volta(s)";
            document.getElementById('btnReset').disabled = false;
            atualizarTabela(data);
          }
        });
    }

    function atualizarTabela(data) {
      if (data.temposVoltas.length === 0) return;

      const tbody = document.getElementById('corpo-tabela');
      tbody.innerHTML = "";

      let tempoAnterior = 0;
      for (let i = 0; i < data.temposVoltas.length; i++) {
        const tempoParcial = data.temposVoltas[i];
        const tempoVolta = tempoParcial - tempoAnterior;
        tempoAnterior = tempoParcial;

        const row = document.createElement('tr');
        row.innerHTML = `<td>${i + 1}</td><td>${tempoParcial.toFixed(3)}</td><td>${tempoVolta.toFixed(3)}</td>`;
        tbody.appendChild(row);
      }

      document.getElementById('tabela-voltas').style.display = 'table';
    }
  </script>
</body>
</html>
)rawliteral";

// ==============================================================================
// SETUP
// ==============================================================================
void setup() {
  Serial.begin(115200);

  // Configura o pino do botao (simulacao do sensor)
  pinMode(PINO_BOTAO, INPUT_PULLDOWN);

#if USAR_SENSOR_LIDAR
  tfLunaSerial.begin(115200, SERIAL_8N1, 16, 17);
#endif

  WiFi.softAP(ssid, password);
  IPAddress apIP = WiFi.softAPIP();
  Serial.print("Servidor no IP: ");
  Serial.println(apIP);

  // Inicia DNS Captive Portal: redireciona qualquer dominio para o IP do AP
  dnsServer.start(53, "*", apIP);

  server.on("/", []() {
    server.send(200, "text/html", html_page);
  });

  // ROTA 1: Sincronizacao dos Relogios
  server.on("/sync", []() {
    if (server.hasArg("t")) {
      unsigned long long tempoCelular = strtoull(server.arg("t").c_str(), NULL, 10);
      offsetTempo = tempoCelular - millis();
      Serial.println("Relogios sincronizados!");
      server.send(200, "text/plain", "OK");
    }
  });

  // ROTA 2: Recebe o Timestamp da Largada e numero de voltas
  server.on("/start", []() {
    if (server.hasArg("t")) {
      tempoLargada = strtoull(server.arg("t").c_str(), NULL, 10);

      // Define numero de voltas
      if (server.hasArg("voltas")) {
        numVoltasTotal = server.arg("voltas").toInt();
        if (numVoltasTotal < 1) numVoltasTotal = 1;
        if (numVoltasTotal > MAX_VOLTAS) numVoltasTotal = MAX_VOLTAS;
      } else {
        numVoltasTotal = 1;
      }

      // Reseta estado das voltas
      voltasCompletadas = 0;
      for (int i = 0; i < MAX_VOLTAS; i++) {
        temposVoltas[i] = 0;
      }
      tempoTotalSegundos = 0.0;

#if USAR_SENSOR_LIDAR
      // Esvazia buffer do sensor
      while (tfLunaSerial.available()) {
        tfLunaSerial.read();
      }
#endif

      estado = 1;
      Serial.print("Largada recebida! Voltas: ");
      Serial.println(numVoltasTotal);
      server.send(200, "text/plain", "OK");
    }
  });

  // ROTA 3: Celular pergunta o status
  server.on("/status", []() {
    server.send(200, "application/json", montarJsonStatus());
  });

  // ROTA 4: Resetar para nova corrida
  server.on("/reset", []() {
    estado = 0;
    voltasCompletadas = 0;
    numVoltasTotal = 1;
    tempoTotalSegundos = 0.0;
    tempoLargada = 0;
    for (int i = 0; i < MAX_VOLTAS; i++) {
      temposVoltas[i] = 0;
    }
    Serial.println("Corrida resetada.");
    server.send(200, "text/plain", "OK");
  });

  // ==============================================================================
  // CAPTIVE PORTAL: Intercepta URLs de deteccao dos smartphones
  // ==============================================================================
  // Android
  server.on("/generate_204", HTTP_GET, handleCaptivePortal);
  server.on("/gen_204", HTTP_GET, handleCaptivePortal);
  // iOS / macOS
  server.on("/hotspot-detect.html", HTTP_GET, handleCaptivePortal);
  server.on("/captive.apple.com", HTTP_GET, handleCaptivePortal);
  server.on("/library/test/success.html", HTTP_GET, handleCaptivePortal);
  // Windows
  server.on("/connecttest.txt", HTTP_GET, handleCaptivePortal);
  server.on("/ncsi.txt", HTTP_GET, handleCaptivePortal);
  // Firefox / Ubuntu
  server.on("/detectportal.firefox.com", HTTP_GET, handleCaptivePortal);
  server.on("/canonical.html", HTTP_GET, handleCaptivePortal);

  // Redireciona QUALQUER outra requisicao desconhecida para a pagina principal
  server.onNotFound(handleCaptivePortal);

  server.begin();

#if USAR_SENSOR_LIDAR
  // Configura TF-Luna para 250Hz
  uint8_t cmd_250Hz[] = {0x5A, 0x06, 0x03, 0xFA, 0x00, 0x00};
  tfLunaSerial.write(cmd_250Hz, 6);
  delay(100);
  uint8_t cmd_save[] = {0x5A, 0x04, 0x11, 0x00};
  tfLunaSerial.write(cmd_save, 4);
  delay(100);
  Serial.println("TF-Luna configurado para 250Hz e salvo!");
#else
  Serial.println("Modo SIMULACAO com botao ativo (pino 4).");
#endif
}

// ==============================================================================
// LOOP
// ==============================================================================
void loop() {
  // Processa requisicoes DNS para o Captive Portal
  dnsServer.processNextRequest();

  server.handleClient();

#if USAR_SENSOR_LIDAR
  // --- MODO SENSOR LIDAR ---
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
            registrarVoltaOuChegada();
          }
        }
      }
    }
  }
#else
  // --- MODO SIMULACAO COM BOTAO ---
  if (estado == 1) {
    int leitura = digitalRead(PINO_BOTAO);

    // Se o estado mudou, reseta o timer de debounce
    if (leitura != ultimoEstadoBotao) {
      lastDebounceTime = millis();
    }

    // Se o estado ficou estavel por tempo suficiente
    if ((millis() - lastDebounceTime) > debounceDelay) {
      // Se o estado estavel mudou
      if (leitura != estadoBotao) {
        estadoBotao = leitura;
        // Aciona apenas na borda de descida (pressionamento)
        if (estadoBotao == LOW) {
          unsigned long long tempoDecorrido = getTempoSincronizado() - tempoLargada;
          // Protecao: impede deteccao imediata apos largada (minimo 1 segundo)
          if (tempoDecorrido > 1000) {
            registrarVoltaOuChegada();
          }
        }
      }
    }

    ultimoEstadoBotao = leitura;
  }
#endif
}

// ==============================================================================
// FUNCAO: Registrar volta ou finalizar corrida
// ==============================================================================
void registrarVoltaOuChegada() {
  unsigned long long tempoAtual = getTempoSincronizado();

  // Calcula tempo decorrido desde a largada
  long long diferenca = (long long)tempoAtual - (long long)tempoLargada;
  if (diferenca < 0) diferenca = 0;

  // Protecao: evita leituras duplicadas ou fora de ordem
  if (voltasCompletadas > 0) {
    long long diffAnterior = (long long)temposVoltas[voltasCompletadas - 1];
    if (diferenca <= diffAnterior) {
      return; // Ignora leitura invalida
    }
  }

  temposVoltas[voltasCompletadas] = (unsigned long long)diferenca;
  voltasCompletadas++;

  tempoTotalSegundos = diferenca / 1000.0;

  Serial.print("Volta ");
  Serial.print(voltasCompletadas);
  Serial.print(" registrada! Tempo parcial: ");
  Serial.print(tempoTotalSegundos, 3);
  Serial.println(" s");

  if (voltasCompletadas >= numVoltasTotal) {
    estado = 2; // Finaliza corrida
    Serial.print("Corrida finalizada! Tempo total: ");
    Serial.print(tempoTotalSegundos, 3);
    Serial.println(" s");
  }
}

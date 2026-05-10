#include <WiFi.h>

// nome e senha da rede que o ESP32 vai criar
const char *ssid = "ESP32_Cronometragem";
const char *password = "teste123456"; // min 8 caract

void setup() {
  Serial.begin(115200);
  Serial.println();

  Serial.println("Configurando o Access Point...");
  
  // inicia o Wi-Fi em modo Access Point (AP)
  WiFi.softAP(ssid, password);

  // pega o IP do ESP32
  IPAddress IP = WiFi.softAPIP();
  
  Serial.print("Rede Wi-Fi criada, Nome: ");
  Serial.println(ssid);
  Serial.print("Endereço IP do ESP32: ");
  Serial.println(IP);
}

void loop() {
  // mostra quantos usuários estão conectados
  static int usuarios_anteriores = 0;
  int usuarios_conectados = WiFi.softAPgetStationNum();

  if (usuarios_conectados != usuarios_anteriores) {
    Serial.print("Dispositivos conectados agora: ");
    Serial.println(usuarios_conectados);
    usuarios_anteriores = usuarios_conectados;
  }
  
  delay(1000);
}
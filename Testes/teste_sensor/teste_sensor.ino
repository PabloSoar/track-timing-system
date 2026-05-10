#include <WiFi.h>

// Nome e senha da rede que o ESP32 vai criar
const char *ssid = "ESP32_Cronometragem";
const char *password = "teste123456"; // A senha precisa ter no mínimo 8 caracteres

void setup() {
  Serial.begin(115200);
  Serial.println();

  Serial.println("Configurando o Access Point...");
  
  // Inicia o Wi-Fi em modo Access Point (AP)
  WiFi.softAP(ssid, password);

  // Pega o endereço de IP do ESP32 (por padrão costuma ser 192.168.4.1)
  IPAddress IP = WiFi.softAPIP();
  
  Serial.print("Rede Wi-Fi criada com sucesso! Nome: ");
  Serial.println(ssid);
  Serial.print("Endereço IP do ESP32: ");
  Serial.println(IP);
}

void loop() {
  // O ESP32 indica quantas pessoas/celulares estão conectados na rede dele
  static int usuarios_anteriores = 0;
  int usuarios_conectados = WiFi.softAPgetStationNum();

  if (usuarios_conectados != usuarios_anteriores) {
    Serial.print("Dispositivos conectados agora: ");
    Serial.println(usuarios_conectados);
    usuarios_anteriores = usuarios_conectados;
  }
  
  delay(1000);
}
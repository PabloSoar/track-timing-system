#include <HardwareSerial.h>

// Inicializar porta Serial 2 do ESP
HardwareSerial tfLunaSerial(2); 

void setup() {
  // Monitor Serial
  Serial.begin(115200);
  
  // Serial do TF-Luna (Baud rate padrão é 115200, pinos RX=16, TX=17)
  tfLunaSerial.begin(115200, SERIAL_8N1, 16, 17); 
  
  Serial.println("Iniciando leitura do TF-Luna...");
}

void loop() {
  // O formato padrão do TF-Luna envia frames de 9 bytes
  if (tfLunaSerial.available() >= 9) {
    
    // O pacote de dados sempre começa com dois bytes de cabeçalho: 0x59 e 0x59
    if (tfLunaSerial.read() == 0x59) {
      if (tfLunaSerial.read() == 0x59) {
        
        // Lê os próximos bytes (Distância, Força do Sinal, Temperatura e Checksum)
        int dist_L = tfLunaSerial.read();
        int dist_H = tfLunaSerial.read();
        int amp_L = tfLunaSerial.read();
        int amp_H = tfLunaSerial.read();
        int temp_L = tfLunaSerial.read();
        int temp_H = tfLunaSerial.read();
        int checksum = tfLunaSerial.read(); // Lemos para esvaziar o buffer, mesmo sem validar (para o sistema real validaremos)

        // A distância real é a combinação do byte Alto (High) e Baixo (Low)
        int distance = dist_L + (dist_H << 8);
        
        // A força do sinal (Amplitude) também usa dois bytes
        int strength = amp_L + (amp_H << 8);

        // Imprime os resultados no computador
        Serial.print("Distância: ");
        Serial.print(distance);
        Serial.print(" cm , Força do Sinal: ");
        Serial.println(strength);
      }
    }
  }
}
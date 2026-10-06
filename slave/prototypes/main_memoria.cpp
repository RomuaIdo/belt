#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;

#define I2C_SDA 8
#define I2C_SCL 9
#define MPU_INT_PIN 7

const uint8_t MAX_ACCELERATION = 15; 

// Função raiz para enviar bytes diretamente aos registradores da MPU
void writeMPURegister(uint8_t reg, uint8_t data) {
  Wire.beginTransmission(0x68); // 0x68 é o endereço I2C padrão da MPU-6050
  Wire.write(reg);
  Wire.write(data);
  Wire.endTransmission();
}

// Reseta a memória FIFO para começar do zero
void resetFIFO() {
  writeMPURegister(0x6A, 0x44); // Ativa o FIFO (bit 6) e comanda o Reset (bit 2)
}

void setup() {
  Serial.begin(115200);
  delay(3000); 

  Wire.begin(I2C_SDA, I2C_SCL);

  while (!mpu.begin()) {
    Serial.println("Falha ao encontrar o MPU-6050!");
    delay(2000);
  }
  
  // 1. Configuração do gatilho de impacto
  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionThreshold(MAX_ACCELERATION);
  mpu.setMotionDetectionDuration(20); 
  mpu.setInterruptPinLatch(true);     
  mpu.setInterruptPinPolarity(false); // Active HIGH
  mpu.setMotionInterrupt(true);       
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G); // Escala de 8G (4096 LSB/g)

  // 2. CONFIGURAÇÃO RAIZ DO FIFO (Sem biblioteca)
  // Registrador 0x19: Taxa de amostragem. 1000Hz / (1 + 99) = 10Hz (Amostras a cada 0.1s)
  writeMPURegister(0x19, 99); 
  
  // Registrador 0x1A: Filtro DLPF = 1 (Garante base de 1kHz) e FIFO_MODE = 0 (Circular Overwrite)
  writeMPURegister(0x1A, 0x01); 
  
  // Registrador 0x23: Habilita gravar TEMP (bit 7) e ACCEL (bit 3) no FIFO = 8 bytes redondos
  writeMPURegister(0x23, 0x88); 
  
  resetFIFO();

  pinMode(MPU_INT_PIN, INPUT_PULLDOWN);
  
  // A ESP32 agora SÓ acorda por impacto, nunca por tempo. Sono infinito real.
  esp_sleep_enable_ext0_wakeup((gpio_num_t)MPU_INT_PIN, 1);
  
  Serial.println("\nSetup raiz concluído! Memória girando. Entrando em sono profundo...");
  Serial.flush();
}

void loop() {
  mpu.getMotionInterruptStatus(); // Limpa gatilho
  
  esp_light_sleep_start();

  // Acordou do impacto!
  delay(2500); // Aguarda Ubuntu reconhecer a porta USB
  
  Serial.println("\n!!! IMPACTO DETECTADO !!! Baixando memória interna da MPU...");

  // Passo 1: Descobrir quantos bytes estão acumulados no FIFO (Registradores 0x72 e 0x73)
  Wire.beginTransmission(0x68); 
  Wire.write(0x72); 
  Wire.endTransmission(false);
  Wire.requestFrom(0x68, 2);
  
  uint16_t fifo_count = (Wire.read() << 8) | Wire.read();
  int total_packets = fifo_count / 8; // Cada amostra tem 8 bytes
  
  // Passo 2: Queremos os últimos 5 segundos. Como a taxa é 10Hz, são 50 pacotes.
  int packets_to_process = min(total_packets, 50);
  int packets_to_discard = total_packets - packets_to_process;

  // Passo 3: Jogar fora as leituras velhas que não importam
  for (int i = 0; i < packets_to_discard; i++) {
    Wire.beginTransmission(0x68); Wire.write(0x74); Wire.endTransmission(false);
    Wire.requestFrom(0x68, 8);
    while(Wire.available()) Wire.read(); // Esvazia o buffer do barramento
  }

  // Passo 4: Ler e processar as 50 leituras cruciais
  int display_counter = 0;
  Serial.println("====== Histórico dos últimos 5 Segundos ======");
  
  for (int i = 0; i < packets_to_process; i++) {
    // Solicita 8 bytes do registrador de saída do FIFO (0x74)
    Wire.beginTransmission(0x68); Wire.write(0x74); Wire.endTransmission(false);
    Wire.requestFrom(0x68, 8);

    // Reconstroi os inteiros de 16-bits juntando os bytes Altos e Baixos
    int16_t ax = (Wire.read() << 8) | Wire.read();
    int16_t ay = (Wire.read() << 8) | Wire.read();
    int16_t az = (Wire.read() << 8) | Wire.read();
    Wire.read(); Wire.read(); // Descartamos os 2 bytes de temperatura

    // Filtramos para imprimir apenas a cada 5 pacotes (0.5s de espaço)
    if (i % 5 == 0) {
      // Converte dados puros da escala de 8G (4096 LSB/g) para Força G
      float x = ax / 4096.0;
      float y = ay / 4096.0;
      float z = az / 4096.0;

      // Trigonométria do ângulo
      float roll = atan2(y, z) * 180.0 / PI;
      float pitch = atan2(-x, sqrt(y * y + z * z)) * 180.0 / PI;

      Serial.print("T-"); 
      Serial.print(5.0 - (display_counter * 0.5), 1);
      Serial.print("s | Roll: "); 
      Serial.print(roll, 1);
      Serial.print("° | Pitch: "); 
      Serial.print(pitch, 1);
      Serial.println("°");
      
      display_counter++;
    }
  }
  
  Serial.println("==============================================");
  
  resetFIFO(); // Limpa a memória física da MPU para começar a gravar limpo
  delay(4000);
}

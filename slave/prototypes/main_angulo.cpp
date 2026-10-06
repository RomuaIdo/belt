#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>

Adafruit_MPU6050 mpu;

// Pinos I2C para a ESP32-S3 Super Mini
#define I2C_SDA 8
#define I2C_SCL 9

void setup() {
  Serial.begin(115200);
  delay(3000); // Tempo para o USB estabilizar no Ubuntu

  Wire.begin(I2C_SDA, I2C_SCL);

  // Em vez de travar silenciosamente, o código vai avisar a cada 2s se os fios estiverem soltos
  while (!mpu.begin()) {
    Serial.println("Falha ao encontrar o MPU-6050! Verifique as conexões GND, 3.3V, SDA(8) e SCL(9).");
    delay(2000);
  }
  
  Serial.println("MPU-6050 conectado com sucesso!");
  
  mpu.setAccelerometerRange(MPU6050_RANGE_8_G);
  mpu.setGyroRange(MPU6050_RANGE_500_DEG);
  mpu.setFilterBandwidth(MPU6050_BAND_21_HZ);
}

void loop() {
  sensors_event_t a, g, temp;
  mpu.getEvent(&a, &g, &temp);

  // Cálculo de inclinação em graus usando trigonometria básica
  // Roll: Inclinação lateral (eixo X)
  float roll = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
  
  // Pitch: Inclinação frontal/traseira (eixo Y)
  float pitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;

  Serial.println("====== Nova Leitura ======");
  Serial.print("Inclinação Lateral (Roll): ");
  Serial.print(roll);
  Serial.println("°");

  Serial.print("Inclinação Frontal (Pitch): ");
  Serial.print(pitch);
  Serial.println("°");
  Serial.println("Aguardando 1 segundos...\n");
  
  // Pausa de 1 segundos exatos (1.000 milissegundos)
  delay(1000); 
}

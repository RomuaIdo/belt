#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_NeoPixel.h>

Adafruit_MPU6050 mpu;

#define I2C_SDA 8
#define I2C_SCL 9
#define MPU_INT_PIN 7

// Aceleração máxima
const uint8_t MAX_ACCELERATION = 10; 

#define LED_PIN    48   // Pino do LED RGB integrado no ESP32-S3 SuperMini
#define NUM_LEDS   1    // Apenas 1 LED RGB na placa

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

void setup() {
  strip.begin();
  strip.show();           // Inicializa o LED apagado
  strip.setBrightness(30);// Brilho de 0 a 255 (reduzido para não ofuscar)

  Serial.begin(115200);
  delay(3000); 

  Wire.begin(I2C_SDA, I2C_SCL);

  while (!mpu.begin()) {
    Serial.println("Falha ao encontrar o MPU-6050!");
    delay(2000);
  }
  
  mpu.setHighPassFilter(MPU6050_HIGHPASS_0_63_HZ);
  mpu.setMotionDetectionThreshold(MAX_ACCELERATION);
  mpu.setMotionDetectionDuration(20); 
  mpu.setInterruptPinLatch(true);     
  
  // false = Active HIGH (Envia 0V em repouso, e dispara 3.3V no impacto)
  mpu.setInterruptPinPolarity(false);  
  
  mpu.setMotionInterrupt(true);       

  // O PULLDOWN aterra o pino internamente, evitando que o vento ou energia estática acordem a placa
  pinMode(MPU_INT_PIN, INPUT_PULLDOWN);

  esp_sleep_enable_ext0_wakeup((gpio_num_t)MPU_INT_PIN, 1);
}

void loop() {
  Serial.println("\nEntrando em Light Sleep... (O monitor serial pode desconectar!)");
  Serial.flush(); 
  
  // Limpa o alerta no MPU antes de dormir, garantindo que o pino 7 volte para 0V
  mpu.getMotionInterruptStatus();
  
  // Entra em hibernação
  esp_light_sleep_start();

  //ligar led:
  // Acende em Verde
  strip.setPixelColor(0, strip.Color(0, 255, 0));
  strip.show();

  delay(2500); 

  //apaga led:
  strip.setPixelColor(0, strip.Color(0, 0, 0));
  strip.show(); 
  
  Serial.println("\n!!! ACORDOU !!! Movimento detectado!");

  // Verifica o impacto
  if (mpu.getMotionInterruptStatus()) {
    sensors_event_t a, g, temp;
    mpu.getEvent(&a, &g, &temp);

    float roll = atan2(a.acceleration.y, a.acceleration.z) * 180.0 / PI;
    float pitch = atan2(-a.acceleration.x, sqrt(a.acceleration.y * a.acceleration.y + a.acceleration.z * a.acceleration.z)) * 180.0 / PI;

    Serial.println("====== Dados da Captura ======");
    Serial.print("Inclinação Lateral (Roll): ");
    Serial.print(roll);
    Serial.println("°");
    Serial.print("Inclinação Frontal (Pitch): ");
    Serial.print(pitch);
    Serial.println("°");
  }
  
  Serial.println("Aguardando 4 segundos para dormir novamente...\n");
  delay(4000);
}

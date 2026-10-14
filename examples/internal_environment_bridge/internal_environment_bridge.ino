// One independent USB serial bridge per sensor. Select BMP390 (1) or SHT45 (0),
// then upload to an Arduino/ESP32 board. Install the matching Adafruit library
// and its library-manager dependencies. Wire.begin() uses the board's SDA/SCL.
#define BRIDGE_BMP390 1
#include <Wire.h>
#if BRIDGE_BMP390
#include <Adafruit_BMP3XX.h>
Adafruit_BMP3XX sensor;
#else
#include <Adafruit_SHT4x.h>
Adafruit_SHT4x sensor;
#endif
bool ready=false;
uint32_t previous=0;
constexpr uint32_t intervalMs=1000;
void setup() {
  Serial.begin(115200);
  Wire.begin();
#if BRIDGE_BMP390
  ready=sensor.begin_I2C(0x77)||sensor.begin_I2C(0x76);
  if(ready) {
    sensor.setTemperatureOversampling(BMP3_OVERSAMPLING_2X);
    sensor.setPressureOversampling(BMP3_OVERSAMPLING_8X);
    sensor.setIIRFilterCoeff(BMP3_IIR_FILTER_COEFF_3);
    sensor.setOutputDataRate(BMP3_ODR_50_HZ);
  }
#else
  ready=sensor.begin();
  if(ready){sensor.setPrecision(SHT4X_HIGH_PRECISION);sensor.setHeater(SHT4X_NO_HEATER);}
#endif
}
void loop() {
  const uint32_t now=millis();
  if(now-previous<intervalMs)return;
  previous=now;
  if(!ready){Serial.println("ERROR: sensor not found");return;}
#if BRIDGE_BMP390
  if(!sensor.performReading()){Serial.println("ERROR: BMP390 read failed");return;}
  Serial.print("T:");Serial.print(sensor.temperature,2);
  Serial.print(" deg C,P:");Serial.print(sensor.pressure,2);Serial.println(" Pa");
#else
  sensors_event_t humidity,temperature;
  if(!sensor.getEvent(&humidity,&temperature)){Serial.println("ERROR: SHT45 read failed");return;}
  Serial.print("Temperature: ");Serial.print(temperature.temperature,2);Serial.println(" degrees C");
  Serial.print("Humidity: ");Serial.print(humidity.relative_humidity,2);Serial.println(" % rH");
#endif
}

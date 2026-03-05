#include <Arduino_LSM6DSOX.h>

const int button_pin = 13;

void setup() {
  Serial.begin(115200);
  while (!Serial) {}
  delay(1000);
  if (!IMU.begin()) {
    Serial.println("Failed to initialize IMU!");
    while (1);
  }

  Serial.println("ax,ay,az,gx,gy,gz");  // CSV header
}

void loop() {
  float ax, ay, az;
  float gx, gy, gz;

  if (IMU.accelerationAvailable() && IMU.gyroscopeAvailable() && digitalRead(button_pin)) {
    IMU.readAcceleration(ax, ay, az);
    IMU.readGyroscope(gx, gy, gz);

    Serial.print(ax * 100, 6); Serial.print(",");
    Serial.print(ay * 100, 6); Serial.print(",");
    Serial.print(az * 100, 6); Serial.print(",");
    Serial.print(gx, 6); Serial.print(",");
    Serial.print(gy, 6); Serial.print(",");
    Serial.println(gz, 6);
  }

  delay(10); // ≈100 Hz, matches good gesture sampling
}

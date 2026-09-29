// Task 9: Arduino-ESP32 serial bring-up only. Framework porting comes later.
void setup() {
  Serial.begin(115200);
}

void loop() {
  Serial.println("Hello from ESP32-S3");
  delay(1000);
}

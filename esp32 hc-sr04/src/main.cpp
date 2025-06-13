#include <Arduino.h>

const int trigPin = 22;
const int echoPin = 23;
const float SOUND_SPEED = 0.034; // cm/us

int getDistance()
{
  long duration;
  int distance;

  digitalWrite(trigPin, LOW);
  delayMicroseconds(2);
  digitalWrite(trigPin, HIGH);
  delayMicroseconds(10);
  digitalWrite(trigPin, LOW);

  duration = pulseIn(echoPin, HIGH, 25000); // 25ms timeout (~4m max distance)
  if (duration == 0) {
    return -1; // No echo received
  }

  distance = duration * SOUND_SPEED / 2;
  return round(distance);
}

void setup()
{
  pinMode(trigPin, OUTPUT);
  pinMode(echoPin, INPUT);
  Serial.begin(115200);
}

void loop()
{
  int distance = getDistance();

  if (distance < 0) {
    Serial.println("Out of range");
  } else {
    Serial.print("Distance: ");
    Serial.print(distance);
    Serial.println(" cm");
  }

  delay(100);
}
#include "commands.h"
#include "cli.h"
#include "digitalWriteFast.h"
#include "encoders.h"

#include "motors.h"
#include "profile.h"
#include "sensors.h"
#include "settings.h"
#include "streaming.h"
#include "systick.h"
#include "twiddle.h"
#include <arduino.h>
#include <util/atomic.h>

void cmdFilter(float f) {
  float iirOut[2];
  float angle = 2 * 3.14159 * f / 500;
  float alpha = 0.995;
  float pole[2];
  pole[0] = cos(angle) * alpha;
  pole[1] = sin(angle) * alpha;
  uint32_t t = millis();
  while (!functionButtonPressed()) {
    while (t == millis()) {
      // do nothing
    }
    t = millis();
    float out[2];
    out[0] = iirOut[0] * pole[0] - iirOut[1] * pole[1] + getSensor(0);
    out[1] = iirOut[0] * pole[1] - iirOut[1] * pole[0];
    iirOut[0] = out[0];
    iirOut[1] = out[1];
    float mag = out[0] * out[0] + out[1] * out[1];
    Serial.println(mag, 4);
  }
}

void cmdLineCalibrate(Args &args) {
  sensorsEnable();
  fwd.reset();
  rot.reset();
  motor_controllers_enabled = true;
  rot.start_move(360.0, 300.0, 0.0, 1000.0);
  int angle = -1;
  int a;
  while (rot.mState != Profile::FINISHED) {
    a = int(rot.mPosition);
    if (a != angle) {
      angle = a;
      Serial << _JUSTIFY(angle, 5);
      sensorsShow();
    }
  }
  motor_controllers_enabled = false;
  stop_motors();
  sensorsDisable();
}
//////////////////////////////////////////////////////////////////////
void sendWallCalHeader() {
  Serial.println();
  Serial.println(F("time position Left Right Front Ctrl Error "));
}
void sendWallCalTelemetry(uint32_t t, float error) {
  int posNow = int(fwd.mPosition);
  // send it at leisure
  Serial.print(t);
  Serial.print(' ');
  Serial.print(posNow);
  Serial.print(' ');
  Serial.print(gSensorLeftWall);
  Serial.print(' ');
  Serial.print(gSensorRightWall);
  Serial.print(' ');
  Serial.print(gSensorFrontWall);
  Serial.print(' ');
  Serial.print(gSteeringControl);
  Serial.print(' ');
  Serial.print(gSensorCTE);
  Serial.print(' ');
  Serial.print(rot.mCurrentSpeed);
  Serial.println();
}

void cmdWallCalibrate(Args &args) {
  uint32_t t = millis();
  float topSpeed = 500;
  float accel = 2000;
  if (args.argc > 1) {
    topSpeed = atof(args.argv[1]);
  }
  if (args.argc > 2) {
    accel = atof(args.argv[2]);
  }
  sendWallCalHeader();
  sensorsEnable();
  fwd.reset();
  rot.reset();
  motor_controllers_enabled = true;
  steeringReset();
  gSteeringEnabled = true;
  fwd.start_move(7 * 180.0, topSpeed, 0.0, accel);
  while (not(fwd.is_finished())) {
    sendWallCalTelemetry(millis() - t, 1);
  }
  gSteeringEnabled = false;
  uint32_t t2 = millis() - t;
  spin(180);
  gSteeringEnabled = true;
  t = millis();
  fwd.start_move(7 * 180.0, topSpeed, 0, accel);
  while (not(fwd.is_finished())) {
    sendWallCalTelemetry(millis() - t + t2, 1);
  }
  gSteeringEnabled = false;
  spin(-180);
  // fwd.make_move(150,500,0,2000);
  motor_controllers_enabled = false;
  stop_motors();
  sensorsDisable();
}
///////////////////////////////////////////////////////////////////////

void cmdShowFront(Args &args) {
}

void cmdShowLeft(Args &args) {
}

void cmdShowRight(Args &args) {
}

void cmdShowBattery(Args &args) {
}

void cmdShowFunction(Args &args) {
}

void cmdShowEncoders(Args &args) {
  Serial.println(F("Encoders:"));
  while (true) {
    Serial.print(F("Left = "));
    Serial.print(encoderLeftCount);
    Serial.print(F("  Right = "));
    Serial.print(encoderRightCount);
    Serial.print(F("  FWD = "));
    Serial.print(encoderTotal);
    Serial.print(F("  ROT = "));
    Serial.print(encoderRotation);
    Serial.println();
    if (functionButtonPressed()) {
      break;
    }
  }
}

/**
 * Open loop forward motion test
 * Move the robot forwards or backwards by setting the motor
 * voltage directly for a given time.
 * During motion, data is transmitted using the serial port and
 * can be collected over a Bluetooth link
 * Requires that the motor controllers be disabled.
 *
 * Command line:
 *   FWD volts time
 *
 * where time is in milliseconds
 * e.g. FWD  4.5 1000  => implies left = +4.5V, right = -4.5V, 1200ms run
 */

void cmdTestFwd(Args &args) {
  uint32_t endTime = 1000;
  int volts = 3;
  if (args.argc > 1) {
    volts = atoi(args.argv[1]);
  }
  if (args.argc > 2) {
    endTime = atol(args.argv[2]);
  }
  encoderReset();
  Serial.println(F("time(ms), encSpeed, encPos"));
  set_left_motor_volts(volts);
  set_right_motor_volts(volts);
  uint32_t t = millis();
  endTime += t;
  while (millis() < endTime) {
    // gather the data quickly
    float spd = encoderSpeed;
    float pos = encoderPosition;
    // send it at leisure
    Serial.print(millis() - t);
    Serial.print(' ');
    Serial.print(spd, 4);
    Serial.print(' ');
    Serial.print(pos, 4);
    Serial.println();
  }
  stop_motors();
}

enum { IDLE,
       STARTING,
       RUNNING,
       STOPPING,
       CROSSING };
void sendLineHeader() {
  Serial.println();
  Serial.println(F("time position Start Left Right Turn Error FastError rawError"));
}
void sendLineTelemetry(uint32_t t, float error) {
  int posNow = int(fwd.mPosition);
  // send it at leisure
  Serial.print(t);
  Serial.print(' ');
  Serial.print(posNow);
  Serial.print(' ');
  Serial.print(getSensor(0));
  Serial.print(' ');
  Serial.print(getSensor(1));
  Serial.print(' ');
  Serial.print(getSensor(2));
  Serial.print(' ');
  Serial.print(getSensor(3));
  Serial.print(' ');
  Serial.print(gSteeringControl);
  Serial.print(' ');
  Serial.print(error);
  Serial.println();
}

float lineTrial() {
  encoderReset();
  sensorsEnable();
  steeringReset();
  fwd.reset();
  rot.reset();
  motor_controllers_enabled = true;
  gSteeringEnabled = true;
  gSteeringControl = 0;
  uint32_t trigger = millis();
  float errorSum = 0;
  uint32_t startTime = millis();
  fwd.start_move(500, 500, 500, 4000);
  enum {
    IDLE,
    STARTING,
    RUNNING,
    STOPPING,
  };
  int runState = STARTING;
  digitalWrite(LED_LEFT, 0);
  digitalWrite(LED_RIGHT, 0);
  float error = 0;
  float slowFilter = 0;
  while (runState != STOPPING) {
    uint8_t markers = startMarker() + 2 * turnMarker();
    digitalWrite(LED_RIGHT, markers & 2);
    digitalWrite(LED_LEFT, markers & 1);
    switch (runState) {
      case STARTING:
        if (markers == 3) {
          runState = CROSSING;
        }
        break;
      case CROSSING:
        if (markers == 0) {
          runState = RUNNING;
        }
        break;
      case RUNNING:
        if (markers == 3) {
          runState = STOPPING;
        }
        break;
    }
    if (fwd.mPosition > 2520) {
      runState = STOPPING;
    }

    if (millis() > trigger) {
      trigger += 5;
      if (functionButtonPressed()) {
        runState = STOPPING;
      }
      slowFilter += 0.3 * (gSteeringControl - slowFilter);
      error = fabsf(slowFilter - gSteeringControl);
      errorSum += error;
      // sendLineTelemetry(millis() - startTime, error);
    }
  }
  while (functionButtonPressed()) {
  }
  Serial.print(F("\nError Sum: "));
  Serial.print(errorSum);
  Serial.println('\n');
  sensorsDisable();
  stop_motors();
  delay(250);
  motor_controllers_enabled = false;
  gSteeringEnabled = false;
  return errorSum;
}

void cmdTestTwiddle(Args &args) {
  setup_motor_controllers();
  float *params[2] = {&settings.lineKP, &settings.lineKD};
  Serial.print(*params[0], 6);
  Serial.print("  ");
  Serial.print(*params[1], 6);
  Serial.print("  ");
  Twiddle tw(2, params, lineTrial);
  Serial.println("TWIDDLING");
  tw.go();
}

float cmdFollowLine(Args &args) {
  float speed = 500;
  if (args.argc > 1) {
    speed = atof(args.argv[1]);
  }
  if (args.argc > 2) {
    settings.lineKP = atof(args.argv[2]);
  }
  if (args.argc > 3) {
    settings.lineKD = atof(args.argv[3]);
  }
  Serial.print(F("\"Line follower kD: "));
  Serial.print(settings.lineKP);
  Serial.print(F("  kD: "));
  Serial.print(settings.lineKD);
  Serial.print(F("  speed: "));
  Serial.print(speed);
  Serial.print(F("\""));
  sendLineHeader();
  encoderReset();
  steeringReset();
  sensorsEnable();
  fwd.reset();
  rot.reset();
  gSteeringControl = 0;
  gSteeringEnabled = true;
  motor_controllers_enabled = true;
  uint32_t trigger = millis();
  float errorSum = 0;
  uint32_t startTime = millis();
  fwd.start_move(500, speed, speed, 4000);

  int runState = STARTING;
  digitalWrite(LED_LEFT, 0);
  digitalWrite(LED_RIGHT, 0);
  float error = 0;
  float slowFilter = 0;
  while (runState != STOPPING) {
    uint8_t markers = startMarker() + 2 * turnMarker();
    digitalWrite(LED_RIGHT, markers & 2);
    digitalWrite(LED_LEFT, markers & 1);
    switch (runState) {
      case STARTING:
        if (markers == 3) {
          runState = CROSSING;
        }
        break;
      case CROSSING:
        if (markers == 0) {
          runState = RUNNING;
        }
        break;
      case RUNNING:
        if (markers == 3) {
          runState = STOPPING;
        }
        break;
    }
    if (fwd.mPosition > 2520) {
      runState = STOPPING;
    }

    if (millis() > trigger) {
      trigger += 5;
      if (functionButtonPressed()) {
        runState = STOPPING;
      }
      slowFilter += 0.3 * (gSteeringControl - slowFilter);
      error = fabsf(slowFilter - gSteeringControl);
      errorSum += error;
      sendLineTelemetry(millis() - startTime, error);
    }
  }
  while (functionButtonPressed()) {
  }
  Serial.print(F("\n==========================\n\nError Sum: \""));
  Serial.print(errorSum);
  Serial.println(F("\"\n"));
  sensorsDisable();
  stop_motors();
  motor_controllers_enabled = false;
  delay(250);
  gSteeringEnabled = false;
  return errorSum;
}

/**
 * Open loop rotational motion test
 * Rotates the robot left (+ve) or right (-ve) by setting the motor
 * voltage directly for a given time.
 * During motion, data is transmitted using the serial port and
 * can be collected over a Bluetoth link
 * Requires that the motor controllers be disabled.
 *
 * Command line:
 *   ROT volts time
 *
 * where time is in milliseconds
 * e.g. FWD 1200 4.5
 */

void cmdTestRot(Args &args) {
  uint32_t endTime = 1500;
  int volts = 3;
  if (args.argc > 1) {
    volts = atoi(args.argv[2]);
  }
  if (args.argc > 2) {
    endTime = atol(args.argv[1]);
  }
  encoderReset();

  set_left_motor_volts(-volts);
  set_right_motor_volts(+volts);
  uint32_t t = millis();
  endTime += t;
  while (millis() < endTime) {
    // gather the data quickly
    float spd = encoderOmega;
    float pos = encoderAngle;
    // send it at leisure
    Serial.print(millis() - t);
    Serial.print(' ');
    Serial.print(spd, 4);
    Serial.print(' ');
    Serial.print(pos, 4);
    Serial.println();
  }
  stop_motors();
}

void sendProfileHeader() {
  Serial.println(F("time setPos setSpeed actPos actSpeed encAngle encOmega LeftVolts RightVolts"));
}

void sendProfileData(int timeStamp, Profile &prof) {
  // gather the data quickly for consistency
  float setPos;
  float setSpeed;
  float actPos;
  float actSpeed;
  float actAngle;
  float actOmega;
  ATOMIC_BLOCK(ATOMIC_RESTORESTATE) {
    setSpeed = prof.mCurrentSpeed;
    setPos = prof.mPosition;
    actPos = encoderPosition;
    actSpeed = encoderSpeed;
    actAngle = encoderAngle;
    actOmega = encoderOmega;
  }
  // send it at leisure
  Serial.print(timeStamp);
  Serial.print(' ');
  Serial.print(setPos);
  Serial.print(' ');
  Serial.print(setSpeed);
  Serial.print(' ');
  Serial.print(actPos);
  Serial.print(' ');
  Serial.print(actSpeed);
  Serial.print(' ');
  Serial.print(actAngle);
  Serial.print(' ');
  Serial.print(actOmega);
  Serial.print(' ');
  Serial.print(fwd_controller.output());
  Serial.print(' ');
  Serial.print(rot_controller.output());
  Serial.println();
}

void cmdTestMove(Args &args) {
  uint32_t t = millis();
  float dist = atof(args.argv[1]);
  float topSpeed = atof(args.argv[2]);
  float endSpeed = atof(args.argv[3]);
  float accel = atof(args.argv[4]);
  if (dist == 0) {
    dist = 400;
  }
  if (topSpeed == 0) {
    topSpeed = 400;
  }
  if (accel == 0) {
    accel = 2000;
  }
  delay(500);
  sendProfileHeader();
  encoderReset();
  sensorsEnable();
  fwd.reset();
  rot.reset();
  fwd_controller.Initialize();
  rot_controller.Initialize();
  motor_controllers_enabled = true;
  t = millis();
  fwd.start_move(dist, topSpeed, endSpeed, accel);
  while (fwd.mState != Profile::FINISHED) {
    sendProfileData(millis() - t, fwd);
  }

  motor_controllers_enabled = false;
  stop_motors();
  sensorsDisable();
  Serial.println();
}

void cmdTestMotors(Args &args) {
  float leftVolts = atof(args.argv[1]);
  float rightVolts = atof(args.argv[2]);
  encoderReset();
  fwd.reset();
  rot.reset();
  set_left_motor_volts(leftVolts);
  set_right_motor_volts(rightVolts);
  while (true) {
    Serial.print(F("Left = "));
    Serial.print(encoderLeftCount);
    Serial.print(F("  Right = "));
    Serial.print(encoderRightCount);
    Serial.print(F("  FWD = "));
    Serial.print(encoderTotal);
    Serial.print(F("  ROT = "));
    Serial.print(encoderRotation);
    Serial.println();
    if (functionButtonPressed()) {
      break;
    }
  }
  stop_motors();
  Serial.println();
}

void cmdTestSpin(Args &args) {
  uint32_t t = millis();
  float dist = atof(args.argv[1]);
  float topSpeed = atof(args.argv[2]);
  float endSpeed = atof(args.argv[3]);
  float accel = atof(args.argv[4]);
  if (dist == 0) {
    dist = 360;
  }
  if (topSpeed == 0) {
    topSpeed = 400;
  }
  if (accel == 0) {
    accel = 1500;
  }
  sendProfileHeader();
  encoderReset();
  sensorsEnable();
  fwd.reset();
  rot.reset();
  motor_controllers_enabled = true;
  rot.start_move(dist, topSpeed, endSpeed, accel);
  while (rot.mState != Profile::FINISHED) {
    sendProfileData(millis() - t, rot);
  }
  motor_controllers_enabled = false;
  stop_motors();
  sensorsDisable();
  Serial.println();
}

void cmdTestTurn(Args &args) {
  uint32_t t = millis();
  float dist = atof(args.argv[1]);
  float speed = atof(args.argv[2]);
  float omega = atof(args.argv[3]);
  float alpha = atof(args.argv[4]);
  if (dist == 0) {
    dist = 360;
  }
  if (speed == 0) {
    speed = 500;
  }
  if (omega == 0) {
    omega = 500;
  }
  if (alpha == 0) {
    alpha = 5000;
  }
  sendProfileHeader();
  gSteeringEnabled = false;
  encoderReset();
  fwd.reset();
  rot.reset();
  motor_controllers_enabled = true;
  fwd.start_move(300, speed, speed, 2500);
  while (!fwd.is_finished()) {
  }

  rot.start_move(dist, omega, 0, alpha);
  while (rot.mState != Profile::FINISHED) {
    sendProfileData(millis() - t, rot);
  }
  fwd.start_move(300, speed, 0, 2500);
  while (!fwd.is_finished()) {
  }
  motor_controllers_enabled = false;
  stop_motors();
  Serial.println();
}

void cmdSearch() {
  encoderReset();
  fwd.reset();
  rot.reset();
  motor_controllers_enabled = true;
  sensorsEnable();
  gSteeringControl = 0;
  gSteeringEnabled = true;
  bool finished = false;
  Serial.println(F("START"));
  while (not(finished)) {
    goHalfCell(false);
    // sensors can be checked here.
    if (not(gLeftWall)) {
      Serial.println(F("LEFT"));
      goHalfCell(true);
      spin(90);
    } else if (not(gFrontWall)) {
      Serial.println(F("FWD"));
      goHalfCell(false);
    } else if (not(gRightWall)) {
      Serial.println(F("RIGHT"));
      goHalfCell(true);
      spin(-90);
    } else {
      Serial.println(F("???"));
      goHalfCell(true);
      spin(180);
    }
  }
  sensorsDisable();
  motor_controllers_enabled = false;
  stop_motors();
}

void turnLeft() {
  bool savedSteering = gSteeringEnabled;
  gSteeringEnabled = false;
  fwd.make_move(90, SEARCH_SPEED, 0, SEARCH_ACCEL);
  spin(90);
  steeringReset();
  gSteeringEnabled = savedSteering;
  fwd.make_move(90, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCEL);
}

void turnRight() {
  bool savedSteering = gSteeringEnabled;
  gSteeringEnabled = false;
  fwd.make_move(90, SEARCH_SPEED, 0, SEARCH_ACCEL);
  spin(-90);
  steeringReset();
  gSteeringEnabled = savedSteering;
  fwd.make_move(90, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCEL);
}

void turnAround() {
  bool savedSteering = gSteeringEnabled;
  gSteeringEnabled = false;
  fwd.start_move(90, SEARCH_SPEED, 0, SEARCH_ACCEL);
  stop_motors();
  delay(25);
  spin(-(330.0 / 2));
  steeringReset();
  gSteeringEnabled = savedSteering;
  fwd.make_move(90, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCEL);
}

void cmdFollowWall() {
  encoderReset();
  fwd.reset();
  rot.reset();
  motor_controllers_enabled = true;
  sensorsEnable();
  gSteeringControl = 0;
  gSteeringEnabled = true;
  bool finished = false;
  Serial.println(F("START"));
  fwd.make_move(90.0, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCEL);
  while (not(finished)) {
    if (not(gLeftWall)) {
      Serial.print(F("turn"));
      turnLeft();
      Serial.println(F(" left"));
    } else if (not(gRightWall)) {
      Serial.print(F("turn "));
      turnRight();
      Serial.println(F(" right"));
    } else if (gSensorFrontWall > 25) {
      Serial.print(F("turn "));
      turnAround();
      Serial.println(F(" around"));
    } else {
      fwd.make_move(180.0, SEARCH_SPEED, SEARCH_SPEED, SEARCH_ACCEL);
    }
  }
  sensorsDisable();
  motor_controllers_enabled = false;
  stop_motors();
}
/***
 * smooth turn the robot left or right by 90 degrees
 */
void turn(const int direction){

};

/***
 * spin turn the robot left or right by 90 degrees on the spot
 */
void spin(const float angle) {
  bool savedSteering = gSteeringEnabled;
  gSteeringEnabled = false;
  rot.start_move(angle, 360.0, 0.0, 1200.0);
  while (rot.mState != Profile::FINISHED) {
  }
  steeringReset();
  gSteeringEnabled = savedSteering;
};

void goHalfCell(const bool stopAtEnd) {
  if (stopAtEnd) {
    fwd.start_move(90.0, SEARCH_SPEED, 0.0, 1500.0);
  } else {
    fwd.start_move(90.0, SEARCH_SPEED, SEARCH_SPEED, 1500.0);
  }
  while (fwd.mState != Profile::FINISHED) {
    delay(1);
  }
};

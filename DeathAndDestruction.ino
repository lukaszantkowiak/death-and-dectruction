/* This example uses the accelerometer in the Zumo 32U4's onboard LSM303D accelerometer to detect
 * contact with an adversary robot in the sumo ring.
 *
 * This example extends the BorderDetect example, which makes use of the line sensors on Zumo 32U4
 * Front Sensor Array to detect the border of the sumo ring.  It also illustrates the use of the
 * motors, pushbuttons, lcd, and buzzer.
 *
 * In loop(), the program reads the x and y components of acceleration (ignoring z), and detects a
 * contact when the magnitude of the 3-period average of the x-y vector exceeds an empirically
 * determined XY_ACCELERATION_THRESHOLD.  On contact detection, the forward speed is increased to
 * FULL_SPEED from the default SEARCH_SPEED, simulating a "fight or flight" response.
 *
 * The program attempts to detect contact only when the Zumo is going straight.  When it is
 * executing a turn at the sumo ring border, the turn itself generates an acceleration in the x-y
 * plane, so the acceleration reading at that time is difficult to interpret for contact detection.
 * Since the Zumo also accelerates forward out of a turn, the acceleration readings are also ignored
 * for MIN_DELAY_AFTER_TURN milliseconds after completing a turn. To further avoid false positives,
 * a MIN_DELAY_BETWEEN_CONTACTS is also specified.
 *
 * This example also contains the following enhancements:
 *
 * - uses the buzzer to play a sound effect ("charge" melody) at start of competition and whenever
 *   contact is made with an opposing robot
 *
 * - randomizes the turn angle on border detection, so that the Zumo executes a more effective
 *   search pattern
 *
 * - supports a FULL_SPEED_DURATION_LIMIT, allowing the robot to switch to a SUSTAINED_SPEED after
 *   a short period of forward movement at FULL_SPEED.  In the example, both speeds are set to 400
 *   (max), but this feature may be useful to prevent runoffs at the turns if the sumo ring surface
 *   is unusually smooth.
 *
 * - logging of accelerometer output to the serial monitor when LOG_SERIAL is #defined.
 *
 * This example also makes use of the public domain RunningAverage library from the Arduino website;
 * the relevant code has been copied into this .ino file and does not need to be downloaded
 * separately.
 */

#include <avr/pgmspace.h>
#include <Wire.h>
#include <Zumo32U4.h>

// #define LOG_SERIAL // write log output to serial port

Zumo32U4LCD lcd;

Zumo32U4ButtonA button;

// Accelerometer Settings
#define RA_SIZE 3  // number of readings to include in running average of accelerometer readings
#define XY_ACCELERATION_THRESHOLD 2400  // for detection of contact (~16000 = magnitude of acceleration due to gravity)

// Reflectance Sensor Settings
#define NUM_SENSORS 5
unsigned int sensor_values[NUM_SENSORS];
// this might need to be tuned for different lighting conditions, surfaces, etc.
#define QTR_THRESHOLD  500 // microseconds
Zumo32U4LineSensors sensors;

// Motor Settings
Zumo32U4Motors motors;

Zumo32U4ProximitySensors proxSensors;

// these might need to be tuned for different motor types
#define REVERSE_SPEED     200 // 0 is stopped, 400 is full speed
#define TURN_SPEED        200
#define SEARCH_SPEED      200
#define SUSTAINED_SPEED   400 // switches to SUSTAINED_SPEED from FULL_SPEED after FULL_SPEED_DURATION_LIMIT ms
#define FULL_SPEED        400
#define STOP_DURATION     100 // ms
#define REVERSE_DURATION  200 // ms
#define TURN_DURATION     300 // ms

#define RIGHT 1
#define LEFT -1

enum ForwardSpeed { SearchSpeed, SustainedSpeed, FullSpeed };
ForwardSpeed _forwardSpeed;  // current forward speed setting
unsigned long full_speed_start_time;
#define FULL_SPEED_DURATION_LIMIT     250  // ms

// Sound Effects
Zumo32U4Buzzer buzzer;
const char fugue[] PROGMEM =
  "! O5 L16 agafaea dac+adaea fa<aa<bac#a dac#adaea f"
  "O6 dcd<b-d<ad<g d<f+d<gd<ad<b- d<dd<ed<f+d<g d<f+d<gd<ad"
  "L8 MS <b-d<b-d MLe-<ge-<g MSc<ac<a ML d<fd<f O5 MS b-gb-g"
  "ML >c#e>c#e MS afaf ML gc#gc# MS fdfd ML e<b-e<b-"
  "O6 L16ragafaea dac#adaea fa<aa<bac#a dac#adaea faeadaca"
  "<b-acadg<b-g egdgcg<b-g <ag<b-gcf<af dfcf<b-f<af"
  "<gf<af<b-e<ge c#e<b-e<ae<ge <fe<ge<ad<fd"
  "O5 e>ee>ef>df>d b->c#b->c#a>df>d e>ee>ef>df>d"
  "e>d>c#>db>d>c#b >c#agaegfe f O6 dc#dfdc#<b c#4";
 // use V0 to suppress sound effect; v15 for max volume

 // Timing
unsigned long loop_start_time;
unsigned long last_turn_time;
unsigned long contact_made_time;
#define MIN_DELAY_AFTER_TURN          400  // ms = min delay before detecting contact event
#define MIN_DELAY_BETWEEN_CONTACTS   1000  // ms = min delay between detecting new contact event

// RunningAverage class
// based on RunningAverage library for Arduino
// source:  http://playground.arduino.cc/Main/RunningAverage
template <typename T>
class RunningAverage
{
  public:
    RunningAverage(void);
    RunningAverage(int);
    ~RunningAverage();
    void clear();
    void addValue(T);
    T getAverage() const;
    void fillValue(T, int);
  protected:
    int _size;
    int _cnt;
    int _idx;
    T _sum;
    T * _ar;
    static T zero;
};

// Accelerometer Class -- extends the LSM303 class to support reading and averaging the x-y acceleration
//   vectors from the onboard LSM303DLHC accelerometer/magnetometer
class Accelerometer : public LSM303
{
  typedef struct acc_data_xy
  {
    unsigned long timestamp;
    int x;
    int y;
    float dir;
  } acc_data_xy;

  public:
    Accelerometer() : ra_x(RA_SIZE), ra_y(RA_SIZE) {};
    ~Accelerometer() {};
    void enable(void);
    void getLogHeader(void);
    void readAcceleration(unsigned long timestamp);
    float len_xy() const;
    float dir_xy() const;
    int x_avg(void) const;
    int y_avg(void) const;
    long ss_xy_avg(void) const;
    float dir_xy_avg(void) const;
  private:
    acc_data_xy last;
    RunningAverage<int> ra_x;
    RunningAverage<int> ra_y;
};

Accelerometer lsm303;
boolean in_contact;  // set when accelerometer detects contact with opposing robot

// forward declaration
void setForwardSpeed(ForwardSpeed speed);









void setup()
{
  sensors.initFiveSensors();
  
  proxSensors.initThreeSensors();

  // Initialize the Wire library and join the I2C bus as a master
  Wire.begin();

  // Initialize LSM303
  lsm303.init();
  lsm303.enable();

#ifdef LOG_SERIAL
  lsm303.getLogHeader();
#endif

  randomSeed((unsigned int) readBatteryMillivolts());

  // Uncomment if necessary to correct motor directions:
  //motors.flipLeftMotor(true);
  //motors.flipRightMotor(true);

  ledYellow(1);
  buzzer.playMode(PLAY_AUTOMATIC);
  waitForButtonAndCountDown(false);

}






void waitForButtonAndCountDown(bool restarting)
{
  ledRed(0);

  ledYellow(1);
  lcd.clear();
  lcd.print(F("Press A"));

  button.waitForButton();

  ledYellow(0);
  lcd.clear();

  // play audible countdown
  for (uint16_t i = 5; i > 0; i--)
  {
    lcd.gotoXY(0, 0);
    lcd.print(i);

    delay(990);
    buzzer.playNote(NOTE_G(3), 50, 12);
  }
  lcd.gotoXY(0, 0);
  lcd.print("Die!");

  // reset loop variables
  in_contact = false;  // 1 if contact made; 0 if no contact or contact lost
  contact_made_time = 0;
  last_turn_time = millis();  // prevents false contact detection on initial acceleration
  _forwardSpeed = SearchSpeed;
  full_speed_start_time = 0;
}




boolean firstMoveDone = false;
boolean ahead = false;
boolean roundGo = false;
unsigned long lastTime = 0;

void loop()
{
  if (button.isPressed())
  {
    // if button is pressed, stop and wait for another press to go again
    motors.setSpeeds(0, 0);
    button.waitForRelease();
    waitForButtonAndCountDown(true);
  }


  loop_start_time = millis();
  lsm303.readAcceleration(loop_start_time);
  sensors.read(sensor_values);
  
  proxSensors.read();
  
    lcd.gotoXY(0,1);
    lcd.print(proxSensors.countsLeftWithLeftLeds());
    lcd.print(proxSensors.countsFrontWithLeftLeds());
    lcd.print(proxSensors.countsFrontWithRightLeds());
    lcd.print(proxSensors.countsRightWithRightLeds());

    boolean randomBool = random(1,3) == 1;

  if ((_forwardSpeed == FullSpeed) && (
 - full_speed_start_time > FULL_SPEED_DURATION_LIMIT))
  {
    setForwardSpeed(SustainedSpeed);
  }

  if (!firstMoveDone) {
    if (randomBool) {
      motors.setSpeeds(-400, -100);
    } else {
      motors.setSpeeds(-100, -400);
    }
    delay(400);
    motors.setSpeeds(-400, -400);
    delay(200);
   
    firstMoveDone = true;
  }

  

  if (sensor_values[0] < QTR_THRESHOLD)
  {
    // if leftmost sensor detects line, reverse and turn to the right
    turn(RIGHT, true);
    lcd.print('B');
    ahead = false;
  }
  else if (sensor_values[NUM_SENSORS - 1] < QTR_THRESHOLD)
  {
    // if rightmost sensor detects line, reverse and turn to the left
    turn(LEFT, true);
    lcd.print('B');
    ahead = false;
  }
  else
  {
    // REAL MEAT

    if (check_for_contact()) {
      roundGo = false;
      berserkerMode();
      lcd.print('C');
    } else if (isOponentMuchLeft()) {
      roundGo = false;
      motors.setSpeeds(-400, 400);
      lcd.print('M');
    } else if (isOponentMuchRight()) {
      roundGo = false;
      motors.setSpeeds(400, -400);
      lcd.print('N');
    } else if (isOponentLeft() && !ahead) {
      roundGo = false;
      lcd.print('L');
      motors.setSpeeds(-400, 400);
      ledRed(0);
    } else if (isOponentRight() && !ahead) {
      roundGo = false;
      lcd.print('R');
      motors.setSpeeds(400, -400);
      ledRed(0);
    } else if (isOponentLeft() && ahead) {
      roundGo = false;
      lcd.print('L');
      motors.setSpeeds(100, 400);
    } else if (isOponentRight() && ahead) {
      roundGo = false;
      lcd.print('R');
      motors.setSpeeds(400, 100);
    } else if (isOponentAhead() || ahead) {
      roundGo = false;
      berserkerMode();
      lcd.print('A');
      ahead = true;
    } else {
      if (!roundGo) {
        lastTime = millis();
        roundGo = true;
        motors.setSpeeds(-400, 400);
      } else if (millis() - lastTime > 1000) {
        motors.setSpeeds(-300, 300);
      }
      
      ledRed(0);
      lcd.print('E');
    }
  }
}

boolean isOponentAhead() {
    uint8_t sum = proxSensors.countsFrontWithRightLeds() + proxSensors.countsFrontWithLeftLeds();
    return sum >= 6;
}

boolean isOponentLeft() {
  return isOponentLeft(true);
}

boolean isOponentLeft(boolean verifyOther) {
  return (!verifyOther || !isOponentRight(false))
    && (proxSensors.countsFrontWithLeftLeds() - 1 > proxSensors.countsFrontWithRightLeds()
    ||  proxSensors.countsLeftWithLeftLeds() - 1 > proxSensors.countsRightWithRightLeds());
}

boolean isOponentRight() { 
  return isOponentRight(true);
}

boolean isOponentRight(boolean verifyOther) {
    return (!verifyOther || !isOponentLeft(false))
    && (proxSensors.countsFrontWithRightLeds() - 1 > proxSensors.countsFrontWithLeftLeds()
    ||  proxSensors.countsRightWithRightLeds() - 1 > proxSensors.countsLeftWithLeftLeds());
}

boolean isOponentMuchLeft() {
  return isOponentMuchLeft(true);
}

boolean isOponentMuchLeft(boolean verifyOther) {
  return (!verifyOther || !isOponentRight(false))
    && (proxSensors.countsFrontWithLeftLeds() < 3 && proxSensors.countsFrontWithRightLeds() < 3
    &&  proxSensors.countsLeftWithLeftLeds() - 1 > proxSensors.countsRightWithRightLeds());
}

boolean isOponentMuchRight() { 
  return isOponentMuchRight(true);
}

boolean isOponentMuchRight(boolean verifyOther) {
    return (!verifyOther || !isOponentLeft(false))
    && (proxSensors.countsFrontWithRightLeds() < 3 && proxSensors.countsFrontWithLeftLeds() < 3
    &&  proxSensors.countsRightWithRightLeds() - 1 > proxSensors.countsLeftWithLeftLeds());
}

// execute turn
// direction:  RIGHT or LEFT
// randomize: to improve searching
void turn(char direction, bool randomize)
{
  // assume contact lost
  on_contact_lost();

  static unsigned int duration_increment = TURN_DURATION / 4;

  // motors.setSpeeds(0,0);
  // delay(STOP_DURATION);
  motors.setSpeeds(-REVERSE_SPEED, -REVERSE_SPEED);
  delay(REVERSE_DURATION);
  motors.setSpeeds(TURN_SPEED * direction, -TURN_SPEED * direction);
  delay(randomize ? TURN_DURATION + (random(8) - 2) * duration_increment : TURN_DURATION);
  int speed = getForwardSpeed();
  motors.setSpeeds(speed, speed);
  last_turn_time = millis();
}

void setForwardSpeed(ForwardSpeed speed)
{
  _forwardSpeed = speed;
  if (speed == FullSpeed) full_speed_start_time = loop_start_time;
}

int getForwardSpeed()
{
  int speed;
  switch (_forwardSpeed)
  {
    case FullSpeed:
      speed = FULL_SPEED;
      break;
    case SustainedSpeed:
      speed = SUSTAINED_SPEED;
      break;
    default:
      speed = SEARCH_SPEED;
      break;
  }
  return speed;
}

// check for contact, but ignore readings immediately after turning or losing contact
bool check_for_contact()
{
  static long threshold_squared = (long) XY_ACCELERATION_THRESHOLD * (long) XY_ACCELERATION_THRESHOLD;
  return (lsm303.ss_xy_avg() >  threshold_squared) && \
    (loop_start_time - last_turn_time > MIN_DELAY_AFTER_TURN) && \
    (loop_start_time - contact_made_time > MIN_DELAY_BETWEEN_CONTACTS);
}

// sound horn and accelerate on contact -- fight or flight
void berserkerMode()
{
  in_contact = true;
  contact_made_time = loop_start_time;
//  setForwardSpeed(FullSpeed);
  motors.setSpeeds(400, 400);
  buzzer.playFromProgramSpace(fugue);
  ledRed(1);
}

// reset forward speed
void on_contact_lost()
{
#ifdef LOG_SERIAL
  Serial.print("contact lost");
  Serial.println();
#endif
  in_contact = false;
  setForwardSpeed(SearchSpeed);
  ledRed(0);
}




































// class Accelerometer -- member function definitions

// enable accelerometer only
// to enable both accelerometer and magnetometer, call enableDefault() instead
void Accelerometer::enable(void)
{
  // Enable Accelerometer
  // 0x27 = 0b00100111
  // Normal power mode, all axes enabled
  writeAccReg(LSM303::CTRL_REG1_A, 0x27);

  if (getDeviceType() == LSM303::device_DLHC)
  writeAccReg(LSM303::CTRL_REG4_A, 0x08); // DLHC: enable high resolution mode
}

void Accelerometer::getLogHeader(void)
{
  Serial.print("millis    x      y     len     dir  | len_avg  dir_avg  |  avg_len");
  Serial.println();
}

void Accelerometer::readAcceleration(unsigned long timestamp)
{
  readAcc();
  if (a.x == last.x && a.y == last.y) return;

  last.timestamp = timestamp;
  last.x = a.x;
  last.y = a.y;

  ra_x.addValue(last.x);
  ra_y.addValue(last.y);

#ifdef LOG_SERIAL
 Serial.print(last.timestamp);
 Serial.print("  ");
 Serial.print(last.x);
 Serial.print("  ");
 Serial.print(last.y);
 Serial.print("  ");
 Serial.print(len_xy());
 Serial.print("  ");
 Serial.print(dir_xy());
 Serial.print("  |  ");
 Serial.print(sqrt(static_cast<float>(ss_xy_avg())));
 Serial.print("  ");
 Serial.print(dir_xy_avg());
 Serial.println();
#endif
}

float Accelerometer::len_xy() const
{
  return sqrt(last.x*a.x + last.y*a.y);
}

float Accelerometer::dir_xy() const
{
  return atan2(last.x, last.y) * 180.0 / M_PI;
}

int Accelerometer::x_avg(void) const
{
  return ra_x.getAverage();
}

int Accelerometer::y_avg(void) const
{
  return ra_y.getAverage();
}

long Accelerometer::ss_xy_avg(void) const
{
  long x_avg_long = static_cast<long>(x_avg());
  long y_avg_long = static_cast<long>(y_avg());
  return x_avg_long*x_avg_long + y_avg_long*y_avg_long;
}

float Accelerometer::dir_xy_avg(void) const
{
  return atan2(static_cast<float>(x_avg()), static_cast<float>(y_avg())) * 180.0 / M_PI;
}

// RunningAverage class
// based on RunningAverage library for Arduino
// source:  http://playground.arduino.cc/Main/RunningAverage
// author:  Rob.Tillart@gmail.com
// Released to the public domain

template <typename T>
T RunningAverage<T>::zero = static_cast<T>(0);

template <typename T>
RunningAverage<T>::RunningAverage(int n)
{
  _size = n;
  _ar = (T*) malloc(_size * sizeof(T));
  clear();
}

template <typename T>
RunningAverage<T>::~RunningAverage()
{
  free(_ar);
}

// resets all counters
template <typename T>
void RunningAverage<T>::clear()
{
  _cnt = 0;
  _idx = 0;
  _sum = zero;
  for (int i = 0; i< _size; i++) _ar[i] = zero;  // needed to keep addValue simple
}

// adds a new value to the data-set
template <typename T>
void RunningAverage<T>::addValue(T f)
{
  _sum -= _ar[_idx];
  _ar[_idx] = f;
  _sum += _ar[_idx];
  _idx++;
  if (_idx == _size) _idx = 0;  // faster than %
  if (_cnt < _size) _cnt++;
}

// returns the average of the data-set added so far
template <typename T>
T RunningAverage<T>::getAverage() const
{
  if (_cnt == 0) return zero; // NaN ?  math.h
  return _sum / _cnt;
}

// fill the average with a value
// the param number determines how often value is added (weight)
// number should preferably be between 1 and size
template <typename T>
void RunningAverage<T>::fillValue(T value, int number)
{
  clear();
  for (int i = 0; i < number; i++)
  {
    addValue(value);
  }
}

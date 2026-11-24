#ifndef BOARD_CONFIG_H
#define BOARD_CONFIG_H
/* M1=left front, M2=left rear, M3=right front, M4=right rear.
 * Raise all wheels and verify each sign before driving on the ground. */
#if ROBOT_WIRING_PROFILE == 2
/* V4.2 bench feedback: all four motors ran opposite to the requested fore/aft. */
#define MOTOR1_SIGN -1
#define MOTOR2_SIGN -1
#define MOTOR3_SIGN  1
#define MOTOR4_SIGN  1
#else
#define MOTOR1_SIGN  1
#define MOTOR2_SIGN  1
#define MOTOR3_SIGN -1
#define MOTOR4_SIGN -1
#endif
/* To test one channel at a time: 0x01, 0x02, 0x04, 0x08; all four: 0x0f. */
#define MOTOR_ENABLE_MASK 0x0f
#define PWM_PERIOD 8399u /* 168 MHz / 8400 = 20 kHz, prescaler 0 */
#endif

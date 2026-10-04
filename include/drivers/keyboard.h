#pragma once

#include <lib/types.h>

#define KEY_UP 1
#define KEY_DOWN 2
#define KEY_LEFT 3
#define KEY_RIGHT 4
#define KEY_HOME 5
#define KEY_END 6
#define KEY_DELETE 7
#define KEY_ERASE_WORD 23 // ctrl held with backspace, or with w
#define KEY_CTRL_S 19     // the numbers are the control codes these have always stood for
#define KEY_CTRL_X 24

#define KEY_ESC 27

void keyboard_install(void);
char get_keyboard_char(void);
int read_number(void);
int read_number_conf(uint8_t number_to_read_len, bool_t overflow_catch);
uint32_t read_hex(void);

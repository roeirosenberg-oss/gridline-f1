#pragma once
#include <stdio.h>
#define log_e(fmt, ...) fprintf(stderr, "    [E] " fmt "\n", ##__VA_ARGS__)
#define log_i(fmt, ...) fprintf(stderr, "    [I] " fmt "\n", ##__VA_ARGS__)
#define log_d(fmt, ...) do {} while (0)
#define log_v(fmt, ...) do {} while (0)

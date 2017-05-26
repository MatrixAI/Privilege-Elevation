#pragma once

#include <stdint.h>

typedef enum {
  PRIVFD = 1
} MechanismProtoType;

typedef struct MechanismProto {
  uint8_t type;
} __attribute__((packed)) MechanismProto;

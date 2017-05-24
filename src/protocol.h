#pragma once

typedef enum {
  PRIVFD = 1
} MechanismProtoType;

typedef struct MechanismProto {
  uint8_t type;
} MechanismProto;

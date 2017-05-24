#pragma once

typedef enum {
  PERMERR = 0,
  PRIVFD = 1
} MechanismProtoType;

typedef struct MechanismProto {
  uint8_t type;
} MechanismProto;

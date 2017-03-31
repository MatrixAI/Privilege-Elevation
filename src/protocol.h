#pragma once

typedef struct MechanismProto {
  enum {PERMERR, PRIVFD} type;
  int data;
} MechanismProto;

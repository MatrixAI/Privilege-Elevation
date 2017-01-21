#pragma once

#include <termios.h>

#ifdef B50
    #define CASEB50(TARGET) case 50: TARGET = B50; break;
#else
    #define CASEB50(TARGET)
#endif

#ifdef B75
    #define CASEB75(TARGET) case 75: TARGET = B75; break;
#else
    #define CASEB75(TARGET)
#endif

#ifdef B110
    #define CASEB110(TARGET) case 110: TARGET = B110; break;
#else
    #define CASEB110(TARGET)
#endif

#ifdef B134
    #define CASEB134(TARGET) case 134: TARGET = B134; break;
#else
    #define CASEB134(TARGET)
#endif

#ifdef B150
    #define CASEB150(TARGET) case 150: TARGET = B150; break;
#else
    #define CASEB150(TARGET)
#endif

#ifdef B200
    #define CASEB200(TARGET) case 200: TARGET = B200; break;
#else
    #define CASEB200(TARGET)
#endif

#ifdef B300
    #define CASEB300(TARGET) case 300: TARGET = B300; break;
#else
    #define CASEB300(TARGET)
#endif

#ifdef B600
    #define CASEB600(TARGET) case 600: TARGET = B600; break;
#else
    #define CASEB600(TARGET)
#endif

#ifdef B1200
    #define CASEB1200(TARGET) case 1200: TARGET = B1200; break;
#else
    #define CASEB1200(TARGET)
#endif

#ifdef B2400
    #define CASEB2400(TARGET) case 2400: TARGET = B2400; break;
#else
    #define CASEB2400(TARGET)
#endif

#ifdef B4800
    #define CASEB4800(TARGET) case 4800: TARGET = B4800; break;
#else
    #define CASEB4800(TARGET)
#endif

#ifdef B9600
    #define CASEB9600(TARGET) case 9600: TARGET = B9600; break;
#else
    #define CASEB9600(TARGET)
#endif

#ifdef B19200
    #define CASEB19200(TARGET) case 19200: TARGET = B19200; break;
#else
    #define CASEB19200(TARGET)
#endif

#ifdef B38400
    #define CASEB38400(TARGET) case 38400: TARGET = B38400; break;
#else
    #define CASEB38400(TARGET)
#endif

#ifdef B57600
    #define CASEB57600(TARGET) case 57600: TARGET = B57600; break;
#else
    #define CASEB57600(TARGET)
#endif

#ifdef B115200
    #define CASEB115200(TARGET) case 115200: TARGET = B115200; break;
#else
    #define CASEB115200(TARGET)
#endif

#ifdef B128000
    #define CASEB128000(TARGET) case 128000: TARGET = B128000; break;
#else
    #define CASEB128000(TARGET)
#endif

#ifdef B230400
    #define CASEB230400(TARGET) case 230400: TARGET = B230400; break;
#else
    #define CASEB230400(TARGET)
#endif

#ifdef B256000
    #define CASEB256000(TARGET) case 256000: TARGET = B256000; break;
#else
    #define CASEB256000(TARGET)
#endif

#ifdef B460800
    #define CASEB460800(TARGET) case 460800: TARGET = B460800; break;
#else
    #define CASEB460800(TARGET)
#endif

#ifdef B500000
    #define CASEB500000(TARGET) case 500000: TARGET = B500000; break;
#else
    #define CASEB500000(TARGET)
#endif

#ifdef B576000
    #define CASEB576000(TARGET) case 576000: TARGET = B576000; break;
#else
    #define CASEB576000(TARGET)
#endif

#ifdef B921600
    #define CASEB921600(TARGET) case 921600: TARGET = B921600; break;
#else
    #define CASEB921600(TARGET)
#endif

#ifdef B1000000
    #define CASEB1000000(TARGET) case 1000000: TARGET = B1000000; break;
#else
    #define CASEB1000000(TARGET)
#endif

#ifdef B1152000
    #define CASEB1152000(TARGET) case 1152000: TARGET = B1152000; break;
#else
    #define CASEB1152000(TARGET)
#endif

#ifdef B1500000
    #define CASEB1500000(TARGET) case 1500000: TARGET = B1500000; break;
#else
    #define CASEB1500000(TARGET)
#endif

#ifdef B2000000
    #define CASEB2000000(TARGET) case 2000000: TARGET = B2000000; break;
#else
    #define CASEB2000000(TARGET)
#endif

#ifdef B2500000
    #define CASEB2500000(TARGET) case 2500000: TARGET = B2500000; break;
#else
    #define CASEB2500000(TARGET)
#endif

#ifdef B3000000
    #define CASEB3000000(TARGET) case 3000000: TARGET = B3000000; break;
#else
    #define CASEB3000000(TARGET)
#endif

#define BAUDSWITCH(SOURCE, TARGET, DEFAULT) \
    switch (SOURCE) { \
        CASEB50(TARGET) \
        CASEB75(TARGET) \
        CASEB110(TARGET) \
        CASEB134(TARGET) \
        CASEB150(TARGET) \
        CASEB200(TARGET) \
        CASEB300(TARGET) \
        CASEB600(TARGET) \
        CASEB1200(TARGET) \
        CASEB2400(TARGET) \
        CASEB4800(TARGET) \
        CASEB9600(TARGET) \
        CASEB19200(TARGET) \
        CASEB38400(TARGET) \
        CASEB57600(TARGET) \
        CASEB115200(TARGET) \
        CASEB128000(TARGET) \
        CASEB230400(TARGET) \
        CASEB256000(TARGET) \
        CASEB460800(TARGET) \
        CASEB500000(TARGET) \
        CASEB576000(TARGET) \
        CASEB921600(TARGET) \
        CASEB1000000(TARGET) \
        CASEB1152000(TARGET) \
        CASEB1500000(TARGET) \
        CASEB2000000(TARGET) \
        CASEB2500000(TARGET) \
        CASEB3000000(TARGET) \
        DEFAULT(TARGET) \
    }

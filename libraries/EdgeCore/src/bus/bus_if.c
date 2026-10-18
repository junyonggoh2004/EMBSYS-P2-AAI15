#include <string.h>
#include "bus/bus_if.h"

// forward declarations for all your buses
extern const bus_t bus_i2c;
extern const bus_t bus_uart;
extern const bus_t bus_gpio;

// table of supported buses
static const bus_t *s_buses[] = {
    &bus_i2c,
    &bus_uart,
    &bus_gpio,
    NULL
};

const bus_t *bus_lookup(const char *name) {
    for (int i = 0; s_buses[i]; i++) {
        if (strcmp(s_buses[i]->name, name) == 0)
            return s_buses[i];
    }
    return NULL; // not found
}

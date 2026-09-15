#ifndef MQTT_JSON_PARSER_H
#define MQTT_JSON_PARSER_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// Try to convert a JSON config payload into a legacy inline block:
//   BEGINCFG|k=v|...|ENDCFG
// Returns:
//   >0 : converted and written to out (N bytes written, excluding '\0')
//    0 : payload is not JSON (first non-space != '{')
//   <0 : error (malformed JSON / overflow)
int mqtt_json_try_convert_to_inline(const char *payload, char *out, size_t out_sz);

#ifdef __cplusplus
}
#endif

#endif /* MQTT_JSON_PARSER_H */

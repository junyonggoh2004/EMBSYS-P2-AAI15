# The shared framework code (libraries/EdgeCore), compiled unchanged: the same
# sources the Pico and Arduino builds use. Nothing is copied.
set(EDGECORE ${CMAKE_CURRENT_LIST_DIR}/../../../libraries/EdgeCore/src)
file(GLOB_RECURSE EDGECORE_SRCS ${EDGECORE}/*.c)
idf_component_register(SRCS ${EDGECORE_SRCS}
                       INCLUDE_DIRS ${EDGECORE}
                       PRIV_REQUIRES edge_mtk)

# IDF builds with -Werror; the Pico and Arduino builds compile this code with
# warnings only. Three existing warnings stay warnings here instead of being
# edited in shared code during the RTOS migration:
#   format   uint32_t printed with %u (uint32_t is unsigned long here; both
#            are 32 bits, so the output is correct)
#   comment  "/*" inside a comment in mqtt_model_transfer.h
#   switch   rule_expr.c parses mse() but never evaluates it (recorded as a
#            reference defect in parity/PARITY.md)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=format -Wno-error=comment -Wno-error=switch)

idf_component_register(SRCS "main.c"
                       INCLUDE_DIRS ""
                       REQUIRES mtkernel edge_mtk esp_system)

# NODE=<id> sh tools/mtk.sh flash passes EDGE_NODE_ID to a separate build
# directory, so one source tree can flash several nodes (D1 tasks 1.1-1.3).
if(EDGE_NODE_ID)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE EDGE_NODE_ID="${EDGE_NODE_ID}")
endif()

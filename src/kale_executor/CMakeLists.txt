# Kale 底层执行器层 (Executor Layer)
# RenderTaskScheduler、TaskChannel、TaskGraph 等
# 依赖：executor 库
# 可选：KALE_EXECUTOR_ENABLE_CHANNELS / KALE_EXECUTOR_ENABLE_TASK_GRAPH 控制通道与任务图扩展

add_library(kale_executor STATIC
    placeholder.cpp
)

target_include_directories(kale_executor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# 可选编译：通道与任务图扩展（phase13-13.3）
target_compile_definitions(kale_executor PUBLIC
    KALE_EXECUTOR_ENABLE_CHANNELS=$<BOOL:${KALE_EXECUTOR_ENABLE_CHANNELS}>
    KALE_EXECUTOR_ENABLE_TASK_GRAPH=$<BOOL:${KALE_EXECUTOR_ENABLE_TASK_GRAPH}>
)

# executor 为必需依赖
if(TARGET executor::executor)
    target_link_libraries(kale_executor PUBLIC executor::executor)
else()
    message(WARNING "executor 未找到，kale_executor 将无法链接。请设置 -DKALE_EXECUTOR_PATH=path/to/executor")
endif()

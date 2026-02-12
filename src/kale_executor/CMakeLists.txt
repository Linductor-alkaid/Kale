# Kale 底层执行器层 (Executor Layer)
# RenderTaskScheduler、TaskChannel、TaskGraph 等
# 依赖：executor 库

add_library(kale_executor STATIC
    placeholder.cpp
    # 待实现：task_scheduler.cpp, task_channel.cpp, task_graph.cpp
)

target_include_directories(kale_executor
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}
)

# executor 为必需依赖
if(TARGET executor::executor)
    target_link_libraries(kale_executor PUBLIC executor::executor)
else()
    message(WARNING "executor 未找到，kale_executor 将无法链接。请设置 -DKALE_EXECUTOR_PATH=path/to/executor")
endif()

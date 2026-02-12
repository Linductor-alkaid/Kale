#!/bin/bash
# kale_auto_dev.sh - 自动化运行多次 Claude Code 开发会话
# 用法: ./kale_auto_dev.sh <次数> [选项]

# ==================== 颜色定义 ====================
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
CYAN='\033[0;36m'
MAGENTA='\033[0;35m'
NC='\033[0m'

# ==================== 配置 ====================
KALE_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LOG_DIR="$KALE_ROOT/.claude_sessions"
TIMESTAMP=$(date +%Y%m%d_%H%M%S)
LOG_FILE="$LOG_DIR/auto_dev_${TIMESTAMP}.log"
SESSION_LOG="$LOG_DIR/session_log.txt"
CLAUDE_BIN="claude"

# 默认参数
NUM_RUNS=1
MODEL="sonnet"
DRY_RUN=false
CONTINUE=false
START_SESSION=1

# ==================== 函数定义 ====================

# 日志函数（简化版，只在文件存在时写入）
log_msg() {
    local level=$1
    local msg="$2"
    local timestamp=$(date '+%Y-%m-%d %H:%M:%S')
    local color=""

    case $level in
        INFO) color="$BLUE" ;;
        SUCCESS) color="$GREEN" ;;
        WARNING) color="$YELLOW" ;;
        ERROR) color="$RED" ;;
        *) color="$NC" ;;
    esac

    echo -e "${color}[$timestamp] [$level]${NC} $msg"
    [ -f "$LOG_FILE" ] && echo "[$timestamp] [$level] $msg" >> "$LOG_FILE"
}

# 分隔线
print_separator() {
    local sep=""
    for ((i=0; i<80; i++)); do sep+="="; done
    echo -e "${CYAN}${sep}${NC}"
    [ -f "$LOG_FILE" ] && echo "${sep}" >> "$LOG_FILE"
}

# 会话标题
print_session_header() {
    local session_num=$1
    local total=$2
    print_separator
    echo -e "${MAGENTA}🚀 开发会话 #${session_num}/${total}${NC}"
    print_separator
    echo ""
    [ -f "$LOG_FILE" ] && {
        echo "=========================================="
        echo "开发会话 #${session_num}/${total}"
        echo "=========================================="
        echo ""
    } >> "$LOG_FILE"
}

# 显示使用说明
show_usage() {
    cat << EOF
${GREEN}用法${NC}: $0 <次数> [选项]

${GREEN}参数${NC}:
    <次数>              要运行的 Claude Code 会话次数

${GREEN}选项${NC}:
    --model <model>     使用的模型 (默认: sonnet，可选: sonnet, opus, haiku)
    --continue          从上次中断处继续
    --dry-run           只显示命令，不实际执行
    -h, --help          显示此帮助信息

${GREEN}示例${NC}:
    $0 5                运行 5 次开发会话
    $0 10 --model opus  使用 Opus 模型运行 10 次
    $0 3 --dry-run      预览将要执行的 3 次会话
    $0 --continue       继续上次的中断

${GREEN}工作流程${NC}:
    每次会话将自动完成以下步骤：
    1. 📖 读取 claude-progress.txt 了解当前状态
    2. 📋 读取 feature_list.json 选择下一个功能
    3. 🔨 实现该功能
    4. ✅ 运行测试验证
    5. 📝 更新状态文件
    6. 💾 提交 git commit

${GREEN}日志位置${NC}:
    - 主日志: $LOG_DIR/auto_dev_<timestamp>.log
    - 会话历史: $SESSION_LOG

EOF
}

# 解析命令行参数
parse_args() {
    # 先处理 --help
    for arg in "$@"; do
        if [ "$arg" = "-h" ] || [ "$arg" = "--help" ]; then
            show_usage
            exit 0
        fi
    done

    if [ $# -eq 0 ]; then
        show_usage
        exit 0
    fi

    # 检查是否是 --continue 模式
    if [ "$1" = "--continue" ]; then
        CONTINUE=true
        if [ -f "$SESSION_LOG" ]; then
            LAST_SESSION=$(grep "开发会话 #" "$SESSION_LOG" 2>/dev/null | tail -1 | grep -oP '#\K\d+' || echo "")
            if [ -n "$LAST_SESSION" ]; then
                START_SESSION=$((LAST_SESSION + 1))
                NUM_RUNS=9999
                echo "继续模式：将从会话 #$START_SESSION 开始"
            else
                echo "错误：无法找到之前的会话记录"
                exit 1
            fi
        else
            echo "错误：会话日志不存在: $SESSION_LOG"
            exit 1
        fi
        shift
    fi

    # 第一个参数是次数
    if [ "$CONTINUE" = false ]; then
        if [[ "$1" =~ ^[0-9]+$ ]]; then
            NUM_RUNS=$1
            shift
        else
            echo "错误：第一个参数必须是数字"
            show_usage
            exit 1
        fi
    fi

    # 解析选项
    while [ $# -gt 0 ]; do
        case $1 in
            --model)
                MODEL="$2"
                shift 2
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            *)
                echo "未知选项: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

# 检查环境
check_environment() {
    log_msg INFO "检查环境..."

    # 检查 claude 命令
    if ! command -v $CLAUDE_BIN &> /dev/null; then
        log_msg ERROR "Claude Code CLI 未找到。请先安装 Claude Code。"
        exit 1
    fi
    log_msg SUCCESS "✓ Claude Code 已安装"

    # 检查必要文件
    if [ ! -f "$KALE_ROOT/feature_list.json" ]; then
        log_msg ERROR "feature_list.json 不存在"
        exit 1
    fi
    log_msg SUCCESS "✓ feature_list.json 存在"

    if [ ! -f "$KALE_ROOT/claude-progress.txt" ]; then
        log_msg ERROR "claude-progress.txt 不存在"
        exit 1
    fi
    log_msg SUCCESS "✓ claude-progress.txt 存在"

    # 创建日志目录
    mkdir -p "$LOG_DIR"
    touch "$LOG_FILE"  # 确保日志文件存在
    log_msg SUCCESS "✓ 日志目录已创建"

    # 检查 git 仓库
    if [ ! -d "$KALE_ROOT/.git" ]; then
        log_msg WARNING "不是 git 仓库，commit 步骤将被跳过"
    else
        log_msg SUCCESS "✓ Git 仓库"
    fi

    # 检查是否有未提交的更改
    if [ -d "$KALE_ROOT/.git" ]; then
        if [ -n "$(git -C "$KALE_ROOT" status --porcelain 2>/dev/null)" ]; then
            log_msg WARNING "存在未提交的更改，建议先提交或暂存"
            git -C "$KALE_ROOT" status --short 2>/dev/null | head -5
        fi
    fi

    echo ""
}

# 生成初始 prompt
generate_prompt() {
    cat << 'EOF'
你是 Kale 渲染引擎项目的 Coding Agent。请按照以下标准工作流程完成一次开发会话：

## 工作流程

### 1. 了解当前状态（必须首先执行）

#### 1.1 验证环境
- 运行 `pwd` 确认工作目录（必须是 /home/linductor/kale）
- 检查是否有未提交的更改：`git status`
- 如果有未提交的更改，先了解情况再决定是否需要处理

#### 1.2 读取状态文件
- 读取 `claude-progress.txt` 了解项目进度
- 读取 `feature_list.json` **验证格式正确**
  - 运行 `python3 -c "import json; json.load(open('feature_list.json'))"` 验证
  - 如果格式错误，使用 `git checkout feature_list.json` 恢复
- 统计当前进度：`grep -c '"status": "completed"' feature_list.json`

#### 1.3 构建状态检查
- 运行 `./init.sh` 或检查构建状态
- 查看 build/ 目录是否存在
- 如果 build/ 目录有问题，删除重建：`rm -rf build && mkdir build && cd build && cmake ..`

### 2. 选择下一个功能
- 在 feature_list.json 中找到一个 status 为 "pending" 的功能
- 确保该功能的所有依赖（dependencies）都已完成
- 优先选择优先级高（priority: "critical" 或 "high"）的功能
- 一次只实现一个功能

### 3. 📖 完整阅读相关文档（重要！）

根据选定的功能，识别其所属的模块（layer 字段），然后按以下顺序阅读：

#### 3.1 识别模块并映射到文档
feature_list.json 中的 layer 字段对应：
- `device_abstraction_layer` → 设备抽象层
- `executor_layer` → 执行器层
- `resource_management_layer` → 资源管理层
- `scene_management_layer` → 场景管理层
- `rendering_pipeline_layer` → 渲染管线层
- `kale_engine` → 引擎层

#### 3.2 阅读文档顺序
**先阅读设计文档：**
1. `docs/design/rendering_engine_design.md` - 项目总设计（必读）
2. `docs/design/<模块>_layer_design.md` - 对应模块的设计文档（必读）
3. `docs/design/rendering_engine_code_examples.md` - 代码示例（如需要）

**再阅读任务清单：**
4. `docs/todolists/project_development_flow.md` - 项目开发流程总览
5. `docs/todolists/<模块>_todolist.md` - 对应模块的详细任务清单

#### 3.3 文档阅读要点
阅读设计文档时，重点关注：
- 模块的职责边界
- 接口定义和设计意图
- 与其他模块的依赖关系
- 关键设计决策和权衡

阅读任务清单时，重点关注：
- 当前功能在整个开发流程中的位置
- 当前功能的依赖关系和后续功能
- 实现细节和验收标准
- 测试方法

### 4. 🔨 实现功能

#### 4.1 实现前准备
- 根据功能描述，在任务清单中找到对应的条目
- 确认要完成的子任务清单（- [ ] 项）
- 理解每个步骤的实现要点

#### 4.2 按步骤实现
- 按照 feature_list.json 中的 steps 逐一实现
- 遵循设计文档中的架构和接口定义
- 参考代码示例中的实现模式
- 保持代码风格与项目一致

#### 4.3 实现要点
- 一次只实现一个功能，不要贪多
- 遵循模块化设计原则
- 注意接口的清晰和完整性
- 添加必要的注释说明设计意图

### 5. ✅ 测试验证

#### 5.1 必须实际执行测试（重要！）
**禁止**：只输出"建议运行..."或"预期输出..."的提示
**必须**：实际执行测试命令并验证结果

测试方法：
```bash
# 构建项目（必须执行）
cd build && cmake --build . -j$(nproc)

# 运行单元测试（如果有）
cd build && ctest --output-on-failure

# 运行示例应用验证
./build/apps/hello_kale/hello_kale
```

#### 5.2 测试文件管理规则
- ❌ **不要**在项目根目录创建 test_* 文件或目录
- ❌ **不要**创建独立的 CMakeLists_test.txt
- ✅ **应该**在 build/ 目录中进行所有测试
- ✅ **应该**使用项目现有的测试框架（tests/ 目录）
- ✅ **应该**测试完成后清理 build 目录中的临时文件

#### 5.3 验证标准
- 根据 feature_list.json 中的 test_verification 进行验证
- **只有测试实际通过后才能标记为完成**
- 如果测试失败，修复问题后重新测试

### 6. 📝 更新文档（重要！）

#### 6.1 更新 feature_list.json（必须完成）
- 将该功能的 status 改为 "completed"
- **不要**删除或修改其他功能
- **不要**修改 features 数组的结构
- 使用 Edit 工具精确修改 status 字段

#### 6.2 更新 claude-progress.txt
在文件顶部添加：
```
[YYYY-MM-DD HH:MM] COMPLETED - feature_id: Feature title
- 实现的主要功能点
- 遇到的问题和解决方案（如有）
- 实际测试结果（必须包含真实输出）
```

#### 6.3 更新任务清单（必须完成）
- 在 `docs/todolists/<模块>_todolist.md` 中
- 将对应的 `- [ ]` 改为 `- [x]`
- 确保子任务的完成状态准确反映
- 只更新本次实现的任务

#### 6.4 清理临时文件（必须完成）
在提交代码前，必须清理：
- 项目根目录的 test_* 目录
- 项目根目录的 test_*.cpp/test_*.c 文件
- 任何临时的测试构建目录
- 验证清理结果：`ls -la` 确保没有残留测试文件

### 7. 💾 提交代码
- 查看修改：`git status`
- 添加文件：`git add .`
- 提交：使用描述性的 commit message

Commit message 格式：
```
<模块>: <简短描述>

<详细描述实现的功能>

- 实现点1
- 实现点2
- 实现点3

Co-Authored-By: Claude Sonnet 4.5 <noreply@anthropic.com>
```

## 重要原则

- **📚 文档优先**：实现前必须完整阅读相关文档
- **🎯 精确定位**：根据任务确定模块，阅读对应文档
- **📋 对照清单**：对照任务清单的子任务逐一完成
- **✏️ 更新文档**：完成后更新 feature_list.json 和任务清单
- **🔍 增量开发**：一次只实现一个功能
- **🧹 清洁状态**：会话结束时代码必须可编译、可运行
- **✅ 完整测试**：标记为完成前必须经过测试验证
- **💾 Git 提交**：每个功能完成后必须提交

## 禁止事项

### 文件和目录管理
- ❌ **不要**在项目根目录创建 test_* 文件或目录
- ❌ **不要**创建独立的测试 CMakeLists.txt
- ❌ **不要**生成多余的文档或说明文件
- ✅ **所有工作记录**都保留在 .claude_sessions/ 中

### 开发流程
- ❌ **不要**跳过文档阅读步骤
- ❌ **不要**只输出"建议运行..."而**不实际执行测试**
- ❌ **不要**一次实现多个功能
- ❌ **不要**在未实际测试的情况下标记功能为完成
- ❌ **不要**留下不可编译的代码

### 状态管理
- ❌ **不要**修改或删除已有的功能项
- ❌ **不要**修改 feature_list.json 的结构
- ❌ **不要**忘记更新 feature_list.json 的 status
- ❌ **不要**忘记更新对应的 todolist.md
- ❌ **不要**会话结束时留下临时文件

### 会话完整性（重要！）
- ✅ **必须**确保 feature_list.json 状态正确更新
- ✅ **必须**确保所有文件都已 git add
- ✅ **必须**确保代码已提交
- ✅ **必须**清理所有临时文件
- ✅ **必须**验证下一个会话可以正常开始

## 模块文档映射速查

| Layer | 设计文档 | 任务清单 |
|-------|---------|---------|
| device_abstraction_layer | device_abstraction_layer_design.md | device_abstraction_layer_todolist.md |
| executor_layer | executor_layer_design.md | executor_layer_todolist.md |
| resource_management_layer | resource_management_layer_design.md | resource_management_layer_todolist.md |
| scene_management_layer | scene_management_layer_design.md | scene_management_layer_todolist.md |
| rendering_pipeline_layer | rendering_pipeline_layer_design.md | rendering_pipeline_layer_todolist.md |
| kale_engine | rendering_engine_design.md (引擎集成部分) | project_development_flow.md |

---

开始工作吧！请先读取状态文件，选择功能，然后**完整阅读相关文档**后再开始实现。
EOF
}

# 运行单次 Claude Code 会话
run_claude_session() {
    local session_num=$1
    local total=$2

    print_session_header $session_num $total

    local prompt_file="$LOG_DIR/prompt_session_${session_num}.txt"
    local output_file="$LOG_DIR/output_session_${session_num}.txt"

    # 会话前清理：删除测试用的临时目录和文件
    log_msg INFO "清理临时文件..."
    find "$KALE_ROOT" -maxdepth 1 -type d -name "test_*" -exec rm -rf {} + 2>/dev/null || true
    find "$KALE_ROOT" -maxdepth 1 -type f -name "test_*.cpp" -delete 2>/dev/null || true
    find "$KALE_ROOT" -maxdepth 1 -type f -name "test_*.c" -delete 2>/dev/null || true
    find "$KALE_ROOT" -maxdepth 1 -type f -name "CMakeLists_test.txt" -delete 2>/dev/null || true

    # 验证 feature_list.json 格式
    if ! python3 -c "import json; json.load(open('$KALE_ROOT/feature_list.json'))" 2>/dev/null; then
        log_msg ERROR "feature_list.json 格式错误，尝试修复..."
        # 备份损坏的文件
        cp "$KALE_ROOT/feature_list.json" "$LOG_DIR/feature_list_backup_${session_num}.json"
        # 尝试使用 git 恢复
        if [ -d "$KALE_ROOT/.git" ]; then
            git -C "$KALE_ROOT" checkout feature_list.json 2>/dev/null || true
        fi
    fi

    # 生成 prompt
    generate_prompt > "$prompt_file"

    # 记录会话开始到日志文件
    {
        echo "=========================================="
        echo "开发会话 #${session_num}/${total}"
        echo "开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
        echo "模型: $MODEL"
        echo "=========================================="
        echo ""
    } >> "$SESSION_LOG"

    log_msg INFO "会话 #$session_num 开始..."
    echo ""

    # 构建 Claude 命令
    local claude_cmd="$CLAUDE_BIN --permission-mode acceptEdits --model $MODEL"

    # 检查是否使用非交互模式
    if [ "${CLAUDE_NON_INTERACTIVE:-false}" = "true" ]; then
        claude_cmd="$claude_cmd --print"
    fi

    claude_cmd="$claude_cmd \"\$(cat $prompt_file)\""

    # 执行或显示命令
    if [ "$DRY_RUN" = true ]; then
        echo -e "${YELLOW}[DRY RUN] 将要执行的命令:${NC}"
        echo "$claude_cmd"
        echo ""
    else
        log_msg INFO "运行 Claude Code..."
        echo ""

        # 运行 Claude 并记录输出
        if eval "$claude_cmd" 2>&1 | tee "$output_file"; then
            local exit_code=${PIPESTATUS[0]}

            if [ $exit_code -eq 0 ]; then
                log_msg SUCCESS "✓ 会话 #$session_num 完成"

                # 会话后清理
                log_msg INFO "清理临时文件..."
                find "$KALE_ROOT" -maxdepth 1 -type d -name "test_*" -exec rm -rf {} + 2>/dev/null || true
                find "$KALE_ROOT" -maxdepth 1 -type f -name "test_*.cpp" -delete 2>/dev/null || true
                find "$KALE_ROOT" -maxdepth 1 -type f -name "test_*.c" -delete 2>/dev/null || true
                find "$KALE_ROOT" -maxdepth 1 -type f -name "CMakeLists_test.txt" -delete 2>/dev/null || true

                # 记录成功
                {
                    echo "完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
                    echo "状态: 成功"
                    echo "输出文件: $output_file"
                    echo ""
                } >> "$SESSION_LOG"
            else
                log_msg ERROR "✗ 会话 #$session_num 失败 (退出码: $exit_code)"

                # 即使失败也尝试清理
                log_msg INFO "清理临时文件..."
                find "$KALE_ROOT" -maxdepth 1 -type d -name "test_*" -exec rm -rf {} + 2>/dev/null || true

                # 记录失败
                {
                    echo "完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
                    echo "状态: 失败 (退出码: $exit_code)"
                    echo "输出文件: $output_file"
                    echo ""
                } >> "$SESSION_LOG"

                return 1
            fi
        else
            log_msg ERROR "✗ 会话 #$session_num 执行失败"

            # 清理
            find "$KALE_ROOT" -maxdepth 1 -type d -name "test_*" -exec rm -rf {} + 2>/dev/null || true

            {
                echo "完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
                echo "状态: 执行失败"
                echo ""
            } >> "$SESSION_LOG"
            return 1
        fi
    fi

    echo ""

    # 会话间暂停
    if [ $session_num -lt $total ]; then
        log_msg INFO "等待 3 秒后开始下一个会话..."
        sleep 3
        echo ""
    fi

    return 0
}

# 显示最终摘要
show_summary() {
    local successful=$1
    local total=$2

    print_separator
    echo -e "${MAGENTA}📊 开发会话摘要${NC}"
    print_separator
    echo ""

    echo "总会话数: $total"
    echo -e "成功: ${GREEN}$successful${NC}"

    if [ $successful -lt $total ]; then
        local failed=$((total - successful))
        echo -e "失败: ${RED}$failed${NC}"
    fi

    echo ""
    echo "日志文件:"
    echo "  - $LOG_FILE"
    echo "  - $SESSION_LOG"
    echo ""

    # Git 统计
    if [ -d "$KALE_ROOT/.git" ]; then
        echo "Git 提交统计:"
        git -C "$KALE_ROOT" log --oneline -10 2>/dev/null | while read commit; do
            echo "  ✓ $commit"
        done
        echo ""
    fi

    # 功能统计
    if [ -f "$KALE_ROOT/feature_list.json" ]; then
        echo "功能完成进度:"
        local total_features=$(grep -c '"id":' "$KALE_ROOT/feature_list.json" 2>/dev/null || echo 0)
        local completed=$(grep -c '"status": "completed"' "$KALE_ROOT/feature_list.json" 2>/dev/null || echo 0)
        local pending=$(grep -c '"status": "pending"' "$KALE_ROOT/feature_list.json" 2>/dev/null || echo 0)

        echo "  总功能数: $total_features"
        echo -e "  ${GREEN}已完成: $completed${NC}"
        echo -e "  ${YELLOW}待完成: $pending${NC}"

        if [ $total_features -gt 0 ]; then
            local percentage=$((completed * 100 / total_features))
            echo "  进度: $percentage%"
        fi
        echo ""
    fi

    print_separator
    echo ""

    if [ $successful -eq $total ]; then
        log_msg SUCCESS "🎉 所有会话成功完成！"
    else
        log_msg WARNING "⚠️  部分会话未完成，请检查日志"
    fi
}

# ==================== 主程序 ====================

main() {
    # 解析参数
    parse_args "$@"

    # 打印标题
    print_separator
    echo -e "${MAGENTA}🤖 Kale 渲染引擎 - 自动化开发系统${NC}"
    print_separator
    echo ""

    echo "配置:"
    echo "  项目目录: $KALE_ROOT"
    echo "  运行次数: $NUM_RUNS"
    echo "  使用模型: $MODEL"
    echo "  开始会话: #$START_SESSION"
    echo "  日志文件: $LOG_FILE"
    if [ "$DRY_RUN" = true ]; then
        echo -e "  ${YELLOW}模式: DRY RUN${NC}"
    fi
    echo ""

    # 检查环境
    check_environment

    # 运行会话
    local successful=0
    local end_session=$((START_SESSION + NUM_RUNS - 1))

    log_msg INFO "开始运行 $NUM_RUNS 次开发会话..."
    echo ""

    for ((i=START_SESSION; i<=end_session; i++)); do
        if run_claude_session $i $end_session; then
            ((successful++))
        else
            log_msg WARNING "会话 #$i 失败，继续下一个会话..."
        fi
    done

    # 显示摘要
    echo ""
    show_summary $successful $NUM_RUNS

    return $((NUM_RUNS - successful))
}

# 运行主程序
main "$@"

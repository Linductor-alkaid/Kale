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

    echo ""
}

# 生成初始 prompt
generate_prompt() {
    cat << 'EOF'
你是 Kale 渲染引擎项目的 Coding Agent。请按照以下标准工作流程完成一次开发会话：

## 工作流程

1. **了解当前状态**
   - 运行 `pwd` 确认工作目录
   - 读取 `claude-progress.txt` 了解项目进度
   - 读取 `feature_list.json` 查看功能列表
   - 运行 `./init.sh` 或检查构建状态

2. **选择下一个功能**
   - 在 feature_list.json 中找到一个 status 为 "pending" 的功能
   - 确保该功能的所有依赖（dependencies）都已完成
   - 优先选择优先级高（priority: "critical" 或 "high"）的功能
   - 一次只实现一个功能

3. **实现功能**
   - 阅读该功能的 description 和 steps
   - 按照步骤逐一实现
   - 如有需要，参考 docs/design/ 中的设计文档
   - 遵循项目的代码风格和架构

4. **测试验证**
   - 构建项目：`cd build && cmake --build . -j$(nproc)`
   - 运行测试（如果有）
   - 运行示例应用验证功能
   - 根据 feature_list.json 中的 test_verification 进行验证

5. **更新状态**
   - 将 feature_list.json 中该功能的 status 改为 "completed"
   - 在 claude-progress.txt 顶部添加本次会话的进度记录
   - 格式：[YYYY-MM-DD HH:MM] COMPLETED - feature_id: Feature title

6. **提交代码**
   - 查看修改：`git status`
   - 添加文件：`git add .`
   - 提交：使用描述性的 commit message

## 重要原则

- **增量开发**：一次只实现一个功能
- **清洁状态**：会话结束时代码必须可编译
- **完整测试**：标记为完成前必须经过测试
- **清晰文档**：更新所有相关的状态文件
- **git commit**：每个功能完成后必须提交

开始工作吧！
EOF
}

# 运行单次 Claude Code 会话
run_claude_session() {
    local session_num=$1
    local total=$2

    print_session_header $session_num $total

    local prompt_file="$LOG_DIR/prompt_session_${session_num}.txt"
    local output_file="$LOG_DIR/output_session_${session_num}.txt"

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

                # 记录成功
                {
                    echo "完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
                    echo "状态: 成功"
                    echo "输出文件: $output_file"
                    echo ""
                } >> "$SESSION_LOG"
            else
                log_msg ERROR "✗ 会话 #$session_num 失败 (退出码: $exit_code)"

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

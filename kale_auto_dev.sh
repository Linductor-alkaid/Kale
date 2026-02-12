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

# CLI 工具（根据 AGENT_MODE 设置）
# agent 模式使用 Cursor CLI，claude 模式使用 Claude CLI

# 默认参数
NUM_RUNS=1
MODEL="sonnet"
DRY_RUN=false
CONTINUE=false
START_SESSION=1
AGENT_MODE="agent"  # agent 或 claude

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
    <次数>              要运行的会话次数

${GREEN}选项${NC}:
    --model <model>     使用的模型 (默认: sonnet，可选: sonnet, opus, haiku)
    --agent <cli>       CLI 工具选择 (默认: agent，可选: agent, claude)
                        - agent: 使用 Cursor CLI (命令: agent)
                        - claude: 使用 Claude Code CLI (命令: claude)
    --continue          从上次中断处继续
    --dry-run           只显示命令，不实际执行
    -h, --help          显示此帮助信息

${GREEN}示例${NC}:
    $0 5                         运行 5 次 Cursor Agent 会话（默认）
    $0 10 --agent claude         运行 10 次 Claude Code 会话
    $0 3 --model opus            使用 Opus 模型运行 3 次
    $0 --dry-run                 预览将要执行的会话
    $0 --continue                从上次中断处继续

${GREEN}CLI 工具说明${NC}:
    agent (Cursor CLI, 默认):
      - 命令: agent -p --force（非交互式/Headless 模式）
      - 多会话需用 -p --force，否则第二次及后续会话会失败
      - 脚本/CI 建议设置 CURSOR_API_KEY 环境变量

    claude (Claude Code CLI):
      - 命令: claude
      - Anthropic Claude Code 命令行工具
      - 更详细的指令跟踪

${GREEN}工作流程${NC}:
    每次会话将自动完成以下步骤：
    1. 📖 读取状态文件了解当前进度
    2. 📋 选择并阅读相关文档
    3. 🔨 实现功能
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
            --agent)
                AGENT_MODE="$2"
                if [ "$AGENT_MODE" != "agent" ] && [ "$AGENT_MODE" != "claude" ]; then
                    echo "错误: --agent 参数必须是 'agent' 或 'claude'"
                    show_usage
                    exit 1
                fi
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

    # 根据 AGENT_MODE 检查对应的 CLI
    local cli_name=""
    if [ "$AGENT_MODE" = "agent" ]; then
        cli_name="agent"
    else
        cli_name="claude"
    fi

    # 检查 CLI 命令
    if ! command -v $cli_name &> /dev/null; then
        log_msg ERROR "$cli_name CLI 未找到。请先安装对应的 CLI 工具。"
        if [ "$AGENT_MODE" = "claude" ]; then
            log_msg ERROR "Claude Code 安装: https://claude.ai/download"
        else
            log_msg ERROR "Cursor CLI: 请安装 Cursor IDE 并确保 agent 命令可用"
        fi
        exit 1
    fi
    log_msg SUCCESS "✓ $cli_name CLI 已安装"

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

# 生成 Agent 模式的 prompt（更自主）

# 生成 Claude 模式的 prompt（更详细的指令）

# 根据模式生成 prompt
generate_prompt() {
    cat << 'EOF'
你是 Kale 渲染引擎项目的开发 Agent。请按照以下工作流程完成一次开发会话：

## 工作流程

1. **了解当前状态**
   - 读取 claude-progress.txt 和 feature_list.json
   - 验证 feature_list.json 格式正确

2. **选择下一个功能**
   - 找一个 status 为 "pending" 的功能
   - 确保依赖已完成
   - 优先级：critical > high > medium

3. **阅读相关文档**
   - docs/design/rendering_engine_design.md (总设计)
   - docs/design/<模块>_layer_design.md (模块设计)
   - docs/todolists/<模块>_todolist.md (任务清单)

4. **实现功能**
   - 按步骤逐一实现
   - 遵循设计文档

5. **测试验证**（必须实际执行）
   ```bash
   cd build && cmake --build . -j$(nproc)
   ```

6. **更新文档**
   - feature_list.json: status → "completed"
   - claude-progress.txt: 添加进度记录
   - todolist.md: 勾选完成的子任务

7. **清理并提交**
   - 清理 test_* 临时文件
   - git add .
   - git commit

## 重要

- ✅ 必须实际执行测试，不能只输出建议
- ❌ 不要在项目根目录创建 test_* 文件
- ✅ 必须清理临时文件
- ✅ 必须更新 feature_list.json

开始工作！
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

    # 根据 AGENT_MODE 选择 CLI 命令
    # agent 模式必须使用 -p --force 以支持非交互式多会话（参见 Cursor Headless CLI 文档）
    local cli_cmd=""
    if [ "$AGENT_MODE" = "agent" ]; then
        cli_cmd="agent -p --force"
    else
        cli_cmd="claude --permission-mode acceptEdits --model $MODEL"
    fi

    # 执行或显示命令
    if [ "$DRY_RUN" = true ]; then
        echo -e "${YELLOW}[DRY RUN] 将要执行的命令:${NC}"
        echo "$cli_cmd \"\$(cat $prompt_file)\""
        echo ""
    else
        log_msg INFO "运行 $AGENT_MODE..."
        echo ""

        # 运行 CLI 并记录输出
        # 使用 PIPESTATUS[0] 获取 agent/claude 的退出码（tee 几乎总是返回 0）
        eval "$cli_cmd \"\$(cat $prompt_file)\"" 2>&1 | tee "$output_file"
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
    fi

    echo ""

    # 会话间暂停（给 Cursor 足够时间清理，避免后续会话失败）
    if [ $session_num -lt $total ]; then
        log_msg INFO "等待 5 秒后开始下一个会话..."
        sleep 5
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
    echo "  CLI 工具: $AGENT_MODE"
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

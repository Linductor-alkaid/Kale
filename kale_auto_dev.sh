#!/bin/bash
# kale_auto_dev.sh - 自动化运行多次 Claude Code 开发会话
# 用法: ./kale_auto_dev.sh <次数> [选项]
# 
# 完全修复版本 v2 - 解决以下问题：
# 1. 移除 eval 使用，直接执行命令
# 2. 正确获取退出码
# 3. 增加会话间隔时间
# 4. 添加 Cursor 进程清理
# 5. 检查必要的环境变量
# 6. 修复会话编号显示错误
# 7. 给予 Agent 完整权限执行所有任务

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
SESSION_INTERVAL=10  # 会话间隔秒数（默认 10 秒）

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
        DEBUG) color="$CYAN" ;;
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
    --interval <秒>     会话间隔时间 (默认: 10 秒)
    --continue          从上次中断处继续
    --dry-run           只显示命令，不实际执行
    -h, --help          显示此帮助信息

${GREEN}示例${NC}:
    $0 5                         运行 5 次 Cursor Agent 会话（默认）
    $0 10 --agent claude         运行 10 次 Claude Code 会话
    $0 3 --model opus            使用 Opus 模型运行 3 次
    $0 5 --interval 15           运行 5 次，每次间隔 15 秒
    $0 --dry-run                 预览将要执行的会话
    $0 --continue                从上次中断处继续

${GREEN}CLI 工具说明${NC}:
    agent (Cursor CLI, 默认):
      - 命令: agent -p --force（非交互式/Headless 模式）
      - 多会话需用 -p --force，否则第二次及后续会话会失败
      - 脚本/CI 建议设置 CURSOR_API_KEY 环境变量
      - 修复: 优化了退出码检测和进程清理

    claude (Claude Code CLI):
      - 命令: claude
      - Anthropic Claude Code 命令行工具
      - 更详细的指令跟踪

${GREEN}工作流程${NC}:
    每次会话将自动完成以下步骤：
    1. 📖 读取状态文件了解当前进度
    2. 📋 选择并阅读相关文档
    3. 🔨 实现功能
    4. ✅ 运行测试验证（完整权限）
    5. 📝 更新状态文件（完整权限）
    6. 💾 提交 git commit（完整权限）

${GREEN}日志位置${NC}:
    - 主日志: $LOG_DIR/auto_dev_<timestamp>.log
    - 会话历史: $SESSION_LOG

${GREEN}修复说明 v2${NC}:
    本版本解决了以下问题：
    - 移除了 eval 命令执行
    - 正确获取命令退出码
    - 增加会话间隔（默认 10 秒）
    - 添加 Cursor 后台进程清理
    - 修复会话编号显示错误（#1/10 → #2/10）
    - 给予 Agent 完整权限执行所有任务

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
                log_msg INFO "继续模式：将从会话 #$START_SESSION 开始"
            else
                log_msg ERROR "无法找到之前的会话记录"
                exit 1
            fi
        else
            log_msg ERROR "会话日志不存在: $SESSION_LOG"
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
            log_msg ERROR "第一个参数必须是数字"
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
                    log_msg ERROR "--agent 参数必须是 'agent' 或 'claude'"
                    show_usage
                    exit 1
                fi
                shift 2
                ;;
            --interval)
                SESSION_INTERVAL="$2"
                if ! [[ "$SESSION_INTERVAL" =~ ^[0-9]+$ ]]; then
                    log_msg ERROR "--interval 参数必须是数字"
                    exit 1
                fi
                shift 2
                ;;
            --dry-run)
                DRY_RUN=true
                shift
                ;;
            *)
                log_msg ERROR "未知选项: $1"
                show_usage
                exit 1
                ;;
        esac
    done
}

# 清理 Cursor 后台进程
cleanup_cursor_processes() {
    if [ "$AGENT_MODE" = "agent" ]; then
        log_msg DEBUG "检查并清理 Cursor 后台进程..."
        
        # 查找可能残留的 agent 进程
        local agent_pids=$(pgrep -f "agent.*--force" 2>/dev/null || true)
        if [ -n "$agent_pids" ]; then
            log_msg WARNING "发现残留的 agent 进程: $agent_pids"
            echo "$agent_pids" | xargs kill -9 2>/dev/null || true
            sleep 2
        fi
        
        # 清理可能的锁文件
        local lock_files=(
            "$HOME/.cursor/agent.lock"
            "$HOME/.cursor/.agent.lock"
            "/tmp/cursor-agent.lock"
        )
        for lock_file in "${lock_files[@]}"; do
            if [ -f "$lock_file" ]; then
                log_msg DEBUG "删除锁文件: $lock_file"
                rm -f "$lock_file" 2>/dev/null || true
            fi
        done
    fi
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

    # 检查 Cursor API Key（对 agent 模式）
    if [ "$AGENT_MODE" = "agent" ]; then
        if [ -z "$CURSOR_API_KEY" ]; then
            log_msg WARNING "未设置 CURSOR_API_KEY 环境变量"
            log_msg WARNING "建议设置以避免认证问题: export CURSOR_API_KEY=your_key"
        else
            log_msg SUCCESS "✓ CURSOR_API_KEY 已设置"
        fi
    fi

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

    # 清理可能残留的进程
    cleanup_cursor_processes

    echo ""
}

# 根据模式生成 prompt
generate_prompt() {
    cat << 'EOF'
你是 Kale 渲染引擎项目的开发 Agent。请按照以下工作流程完成一次开发会话。

## 重要：你拥有完整的执行权限

你被授予完整权限执行以下所有操作，不要只是建议或说明，而是实际执行：
- ✅ 编译和构建项目
- ✅ 运行测试和验证
- ✅ 修改和更新所有文档文件
- ✅ 执行 git add 和 git commit
- ✅ 清理临时文件
- ✅ 修复编译错误和警告

**必须实际执行这些操作，不要留给用户手动完成！**

## 工作流程

### 1. **了解当前状态**
   - 读取 claude-progress.txt 和 feature_list.json
   - 验证 feature_list.json 格式正确（如果损坏则修复）

### 2. **选择下一个功能**
   - 找一个 status 为 "pending" 的功能
   - 确保依赖已完成
   - 优先级：critical > high > medium

### 3. **阅读相关文档**
   - docs/design/rendering_engine_design.md (总设计)
   - docs/design/<模块>_layer_design.md (模块设计)
   - docs/todolists/<模块>_todolist.md (任务清单)

### 4. **实现功能**
   - 按步骤逐一实现
   - 遵循设计文档
   - 确保代码质量

### 5. **测试验证**（必须实际执行）
   ```bash
   cd build
   cmake --build . -j$(nproc)
   ```
   - 必须实际运行构建命令
   - 检查编译输出，确保无错误
   - 如有错误，立即修复并重新构建
   - 如果需要，运行可执行文件验证功能

### 6. **更新文档**（必须实际执行）
   你必须实际修改以下文件：
   
   a) **feature_list.json**：
   ```bash
   # 使用文本编辑工具或脚本将对应功能的 status 改为 "completed"
   # 确保 JSON 格式正确
   ```
   
   b) **claude-progress.txt**：
   ```bash
   # 在文件顶部添加新的进度记录
   echo "=== $(date '+%Y-%m-%d %H:%M:%S') ===" | cat - claude-progress.txt > temp && mv temp claude-progress.txt
   echo "功能：<功能名称>" | cat - claude-progress.txt > temp && mv temp claude-progress.txt
   echo "状态：已完成" | cat - claude-progress.txt > temp && mv temp claude-progress.txt
   echo "" | cat - claude-progress.txt > temp && mv temp claude-progress.txt
   ```
   
   c) **todolist.md**：
   ```bash
   # 将相关子任务从 - [ ] 改为 - [x]
   sed -i 's/- \[ \] <子任务描述>/- [x] <子任务描述>/' docs/todolists/<模块>_todolist.md
   ```

### 7. **清理并提交**（必须实际执行）
   ```bash
   # 清理临时文件
   find . -maxdepth 1 -name "test_*" -type f -delete
   find . -maxdepth 1 -name "test_*" -type d -exec rm -rf {} +
   
   # Git 提交
   git add .
   git commit -m "feat(<phase>): <功能描述>"
   ```
   
   **重要**：你必须实际执行这些命令，不要只是输出建议！

## 严格要求

- ✅ **必须实际执行**测试构建，不能只输出建议
- ✅ **必须实际修改**文档文件，不能只说明需要修改
- ✅ **必须实际执行** git add 和 git commit
- ❌ **不要在项目根目录创建** test_* 文件
- ✅ **必须清理**所有临时文件
- ✅ **必须更新** feature_list.json 的状态

## 完成标准

本次会话被视为成功完成，当且仅当：
1. ✅ 功能已实际实现并通过编译
2. ✅ feature_list.json 已被实际修改（status → "completed"）
3. ✅ claude-progress.txt 已被实际更新
4. ✅ todolist.md 已被实际更新
5. ✅ 已执行 git commit
6. ✅ 所有临时文件已清理

**不要将任何步骤留给用户手动完成，你有完整权限执行所有操作！**

开始工作！
EOF
}

# 运行单次开发会话
run_claude_session() {
    local session_num=$1
    local total_sessions=$2  # 修复：使用明确的变量名表示总会话数

    print_session_header $session_num $total_sessions

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
        echo "开发会话 #${session_num}/${total_sessions}"
        echo "开始时间: $(date '+%Y-%m-%d %H:%M:%S')"
        echo "CLI 工具: $AGENT_MODE"
        echo "模型: $MODEL"
        echo "=========================================="
        echo ""
    } >> "$SESSION_LOG"

    log_msg INFO "会话 #$session_num 开始..."
    log_msg DEBUG "Prompt 文件: $prompt_file"
    log_msg DEBUG "输出文件: $output_file"
    echo ""

    # 执行或显示命令
    if [ "$DRY_RUN" = true ]; then
        echo -e "${YELLOW}[DRY RUN] 将要执行的命令:${NC}"
        if [ "$AGENT_MODE" = "agent" ]; then
            echo "agent -p --force \"\$(cat $prompt_file)\""
        else
            echo "claude --permission-mode acceptEdits --model $MODEL \"\$(cat $prompt_file)\""
        fi
        echo ""
        return 0
    fi

    log_msg INFO "运行 $AGENT_MODE CLI..."
    echo ""

    # 根据 AGENT_MODE 执行命令
    # 修复：直接执行命令，不使用 eval，正确获取退出码
    local exit_code=0
    
    if [ "$AGENT_MODE" = "agent" ]; then
        # Cursor Agent 模式 - 使用 -p --force 确保非交互式
        cd "$KALE_ROOT" || exit 1
        agent -p --force "$(cat "$prompt_file")" > "$output_file" 2>&1
        exit_code=$?
    else
        # Claude Code 模式
        cd "$KALE_ROOT" || exit 1
        claude --permission-mode acceptEdits --model "$MODEL" "$(cat "$prompt_file")" > "$output_file" 2>&1
        exit_code=$?
    fi

    # 显示输出
    cat "$output_file"
    echo ""

    # 检查执行结果
    if [ $exit_code -eq 0 ]; then
        log_msg SUCCESS "✓ 会话 #$session_num 完成 (退出码: $exit_code)"

        # 会话后清理
        log_msg INFO "清理临时文件..."
        find "$KALE_ROOT" -maxdepth 1 -type d -name "test_*" -exec rm -rf {} + 2>/dev/null || true
        find "$KALE_ROOT" -maxdepth 1 -type f -name "test_*.cpp" -delete 2>/dev/null || true
        find "$KALE_ROOT" -maxdepth 1 -type f -name "test_*.c" -delete 2>/dev/null || true
        find "$KALE_ROOT" -maxdepth 1 -type f -name "CMakeLists_test.txt" -delete 2>/dev/null || true

        # 记录成功
        {
            echo "完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
            echo "状态: 成功 (退出码: $exit_code)"
            echo "输出文件: $output_file"
            echo ""
        } >> "$SESSION_LOG"
    else
        log_msg ERROR "✗ 会话 #$session_num 失败 (退出码: $exit_code)"
        log_msg ERROR "详细信息请查看: $output_file"

        # 即使失败也尝试清理
        log_msg INFO "清理临时文件..."
        find "$KALE_ROOT" -maxdepth 1 -type d -name "test_*" -exec rm -rf {} + 2>/dev/null || true

        # 清理可能残留的进程
        cleanup_cursor_processes

        # 记录失败
        {
            echo "完成时间: $(date '+%Y-%m-%d %H:%M:%S')"
            echo "状态: 失败 (退出码: $exit_code)"
            echo "输出文件: $output_file"
            echo ""
        } >> "$SESSION_LOG"

        return 1
    fi

    echo ""

    # 会话间暂停（给 CLI 工具足够时间清理）
    if [ $session_num -lt $total_sessions ]; then
        log_msg INFO "等待 $SESSION_INTERVAL 秒后开始下一个会话..."
        
        # 清理进程
        cleanup_cursor_processes
        
        # 倒计时显示
        for ((i=SESSION_INTERVAL; i>0; i--)); do
            echo -ne "\r剩余 $i 秒...  "
            sleep 1
        done
        echo -e "\r\033[K"  # 清除倒计时行
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
        echo "Git 提交统计 (最近 10 次):"
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
    echo -e "${MAGENTA}🤖 Kale 渲染引擎 - 自动化开发系统 (完全修复版 v2)${NC}"
    print_separator
    echo ""

    echo "配置:"
    echo "  项目目录: $KALE_ROOT"
    echo "  运行次数: $NUM_RUNS"
    echo "  CLI 工具: $AGENT_MODE"
    echo "  使用模型: $MODEL"
    echo "  开始会话: #$START_SESSION"
    echo "  会话间隔: $SESSION_INTERVAL 秒"
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

    log_msg INFO "开始运行 $NUM_RUNS 次开发会话（从 #$START_SESSION 到 #$end_session）..."
    echo ""

    # 修复：正确传递总会话数
    for ((i=START_SESSION; i<=end_session; i++)); do
        if run_claude_session $i $end_session; then
            ((successful++))
        else
            log_msg WARNING "会话 #$i 失败，继续下一个会话..."
            
            # 失败后额外等待，确保环境清理
            log_msg INFO "等待额外 5 秒确保环境清理..."
            sleep 5
        fi
    done

    # 显示摘要
    echo ""
    show_summary $successful $NUM_RUNS

    return $((NUM_RUNS - successful))
}

# 运行主程序
main "$@"
#!/bin/bash
# =====================================================================
# Deployment Script for X210 (S5PV210)
# Transfer and install voiceprint_lock to target board
# =====================================================================
set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/.." && pwd)"
BUILD_DIR="$PROJECT_ROOT/build"
DEPLOY_TAR="$BUILD_DIR/deploy/voiceprint_deploy.tar.gz"

# Target configuration
TARGET_HOST="192.168.1.100"  # X210 IP address
TARGET_USER="root"
TARGET_PORT="22"
TARGET_DIR="/opt/voiceprint"
TARGET_SD="/sdcard"

# Colors
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m'

log_info() { echo -e "${GREEN}[INFO]${NC} $1"; }
log_warn() { echo -e "${YELLOW}[WARN]${NC} $1"; }
log_error() { echo -e "${RED}[ERROR]${NC} $1"; }

usage() {
    cat << EOF
Usage: $0 [OPTIONS]

Deploy voiceprint_lock to X210 target board.

OPTIONS:
    -h, --host HOST     Target IP address (default: $TARGET_HOST)
    -u, --user USER     Target user (default: $TARGET_USER)
    -p, --port PORT     SSH port (default: $TARGET_PORT)
    -t, --tarball FILE  Use existing tarball instead of building
    -k, --key KEY       SSH private key
    -d, --dir DIR       Target installation directory (default: $TARGET_DIR)
    -s, --sd SD_MOUNT   SD card mount point (default: $TARGET_SD)
    --scp-only          Only copy files, don't run setup
    --serial PORT       Deploy via serial port instead of network
    --help              Show this help

EXAMPLES:
    $0 -h 192.168.1.100
    $0 -h 192.168.1.100 -k ~/.ssh/id_rsa
    $0 --scp-only -h 192.168.1.100
    $0 --serial /dev/ttyUSB0

EOF
}

# Parse arguments
SCP_ONLY=false
USE_SERIAL=false
SSH_KEY=""
while [[ $# -gt 0 ]]; do
    case $1 in
        -h|--host) TARGET_HOST="$2"; shift 2 ;;
        -u|--user) TARGET_USER="$2"; shift 2 ;;
        -p|--port) TARGET_PORT="$2"; shift 2 ;;
        -t|--tarball) DEPLOY_TAR="$2"; shift 2 ;;
        -k|--key) SSH_KEY="-i $2"; shift 2 ;;
        -d|--dir) TARGET_DIR="$2"; shift 2 ;;
        -s|--sd) TARGET_SD="$2"; shift 2 ;;
        --scp-only) SCP_ONLY=true; shift ;;
        --serial) USE_SERIAL=true; SERIAL_PORT="$2"; shift 2 ;;
        --help) usage; exit 0 ;;
        *) log_error "Unknown option: $1"; usage; exit 1 ;;
    esac
done

# Check prerequisites
check_prereqs() {
    log_info "Checking prerequisites..."

    # Check tarball exists
    if [ ! -f "$DEPLOY_TAR" ]; then
        log_error "Deploy tarball not found: $DEPLOY_TAR"
        log_info "Run build.sh first: ./scripts/build.sh"
        exit 1
    fi

    # Check tools
    if ! command -v scp &> /dev/null; then
        log_error "scp not found. Please install openssh-client."
        exit 1
    fi

    if ! command -v ssh &> /dev/null; then
        log_error "ssh not found. Please install openssh-client."
        exit 1
    fi

    if [ "$USE_SERIAL" = true ]; then
        if [ ! -e "$SERIAL_PORT" ]; then
            log_error "Serial port not found: $SERIAL_PORT"
            exit 1
        fi
        if ! command -v sz &> /dev/null; then
            log_error "sz (lrzsz) not found on host."
            log_info "Install: sudo apt install lrzsz"
            exit 1
        fi
    fi

    log_info "Prerequisites OK"
}

# Deploy via network (scp/ssh)
deploy_network() {
    log_info "Deploying via network to $TARGET_HOST..."

    # Test connection
    if ! timeout 5 ssh $SSH_KEY -p "$TARGET_PORT" "${TARGET_USER}@${TARGET_HOST}" "echo OK" &> /dev/null; then
        log_warn "Cannot connect to $TARGET_HOST. Check network settings."
        log_info "Make sure the X210 is powered on and SSH server is running."
        read -p "Continue anyway? [y/N] " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi

    # Create remote directory
    ssh $SSH_KEY -p "$TARGET_PORT" "${TARGET_USER}@${TARGET_HOST}" \
        "mkdir -p $TARGET_DIR $TARGET_SD/voiceprint"

    # Transfer files
    log_info "Transferring files..."
    scp $SSH_KEY -P "$TARGET_PORT" "$DEPLOY_TAR" "${TARGET_USER}@${TARGET_HOST}:$TARGET_DIR/"

    # Extract and install
    log_info "Installing on target..."
    ssh $SSH_KEY -p "$TARGET_PORT" "${TARGET_USER}@${TARGET_HOST}" << 'REMOTE_CMDS'
        set -e
        cd /opt/voiceprint
        tar xzf voiceprint_deploy.tar.gz
        chmod +x scripts/run.sh
        chmod +x bin/voiceprint_lock

        # Create SD card directories
        mkdir -p /sdcard/voiceprint/models
        mkdir -p /sdcard/voiceprint/logs

        # Initialize database if not exists
        if [ ! -f /sdcard/voiceprint/voiceprint.db ]; then
            echo "Initializing database..."
        fi

        echo "Installation complete!"
        ls -la /opt/voiceprint/
REMOTE_CMDS

    if [ "$SCP_ONLY" = false ]; then
        log_info "Running setup on target..."
        ssh $SSH_KEY -p "$TARGET_PORT" "${TARGET_USER}@${TARGET_HOST}" << 'REMOTE_SETUP'
            # Create init script for auto-start
            cat > /etc/init.d/voiceprint << 'INIT'
#!/bin/sh
case "$1" in
    start)
        echo "Starting voiceprint_lock..."
        /opt/voiceprint/scripts/run.sh &
        ;;
    stop)
        echo "Stopping voiceprint_lock..."
        killall voiceprint_lock 2>/dev/null || true
        ;;
    restart)
        $0 stop
        $0 start
        ;;
    *)
        echo "Usage: $0 {start|stop|restart}"
        exit 1
esac
INIT
            chmod +x /etc/init.d/voiceprint
            ln -sf /etc/init.d/voiceprint /etc/rc.d/S90voiceprint 2>/dev/null || true
INIT_SETUP
        log_info "Setup complete!"
    fi

    log_info "=== Deployment Successful ==="
    log_info "Binary: $TARGET_DIR/bin/voiceprint_lock"
    log_info "Config: $TARGET_DIR/config/voiceprint.conf"
    log_info "Run:    $TARGET_DIR/scripts/run.sh"
}

# Deploy via serial port (using lrzsz)
deploy_serial() {
    log_info "Deploying via serial port $SERIAL_PORT..."

    log_warn "Serial deployment requires manual intervention on target:"
    log_warn "1. On X210 console, navigate to /opt/voiceprint"
    log_warn "2. Run: rz -y (to receive files)"
    log_warn "3. Then press Enter to start transfer..."

    read -p "Press Enter when ready..."

    # Send tarball via sx (lrzsz)
    sx -b -y "$DEPLOY_TAR" < "$SERIAL_PORT" > "$SERIAL_PORT" 2>/dev/null &

    log_info "Transferring... (this may take a while)"
    log_info "On target: tar xzf voiceprint_deploy.tar.gz"

    log_info "=== Manual Deployment Steps ==="
    cat << 'MANUAL'
    On X210 console:
    1. cd /opt/voiceprint
    2. rz -y (receive tarball)
    3. tar xzf voiceprint_deploy.tar.gz
    4. chmod +x bin/voiceprint_lock scripts/run.sh
    5. mkdir -p /sdcard/voiceprint/{models,logs}
    6. ./scripts/run.sh (test run)
MANUAL
}

# Quick test on target
test_target() {
    log_info "Running quick test on target..."

    ssh $SSH_KEY -p "$TARGET_PORT" "${TARGET_USER}@${TARGET_HOST}" << 'REMOTE_TEST'
        set -e
        echo "=== System Info ==="
        uname -a
        echo ""
        echo "=== Disk Space ==="
        df -h
        echo ""
        echo "=== Binary Check ==="
        file /opt/voiceprint/bin/voiceprint_lock
        echo ""
        echo "=== Testing execution (should fail gracefully) ==="
        cd /opt/voiceprint
        ./bin/voiceprint_lock -c config/voiceprint.conf || echo "Exit code: $?"
REMOTE_TEST
}

# Main
main() {
    log_info "=== Voiceprint Lock Deployment ==="

    check_prereqs

    if [ "$USE_SERIAL" = true ]; then
        deploy_serial
    else
        deploy_network
        if [ "$SCP_ONLY" = false ]; then
            test_target
        fi
    fi
}

main "$@"

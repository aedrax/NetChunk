#!/bin/bash

# NetChunk FTP Test Server Management Script
# This script manages Docker containers for FTP server testing

set -e

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
COMPOSE_FILE="$SCRIPT_DIR/docker-compose.yml"

# Color codes for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
BLUE='\033[0;34m'
NC='\033[0m' # No Color

log_info() {
    echo -e "${BLUE}[INFO]${NC} $1"
}

log_success() {
    echo -e "${GREEN}[SUCCESS]${NC} $1"
}

log_warning() {
    echo -e "${YELLOW}[WARNING]${NC} $1"
}

log_error() {
    echo -e "${RED}[ERROR]${NC} $1"
}

check_docker() {
    if ! command -v docker &> /dev/null; then
        log_error "Docker is not installed or not in PATH"
        exit 1
    fi

    if ! docker info &> /dev/null; then
        log_error "Docker daemon is not running or not accessible"
        exit 1
    fi

    if ! command -v docker-compose &> /dev/null && ! docker compose version &> /dev/null; then
        log_error "Docker Compose is not installed or not in PATH"
        exit 1
    fi
}

start_servers() {
    log_info "Starting NetChunk FTP test servers..."
    
    # Use docker compose if available, fallback to docker-compose
    if docker compose version &> /dev/null; then
        docker compose -f "$COMPOSE_FILE" up -d
    else
        docker-compose -f "$COMPOSE_FILE" up -d
    fi
    
    log_info "Waiting for FTP servers to start..."
    sleep 10
    
    # Check if all containers are running
    local failed_containers=0
    for i in {1..7}; do
        if ! docker ps --format "table {{.Names}}" | grep -q "netchunk-ftp-$i"; then
            log_error "Container netchunk-ftp-$i failed to start"
            ((failed_containers++))
        else
            log_success "Container netchunk-ftp-$i is running"
        fi
    done
    
    if [ $failed_containers -gt 0 ]; then
        log_error "$failed_containers containers failed to start"
        return 1
    fi
    
    log_success "All FTP servers started successfully"
    show_server_info
}

stop_servers() {
    log_info "Stopping NetChunk FTP test servers..."
    
    if docker compose version &> /dev/null; then
        docker compose -f "$COMPOSE_FILE" down
    else
        docker-compose -f "$COMPOSE_FILE" down
    fi
    
    log_success "All FTP servers stopped"
}

restart_servers() {
    log_info "Restarting NetChunk FTP test servers..."
    stop_servers
    sleep 2
    start_servers
}

show_status() {
    log_info "NetChunk FTP Test Server Status:"
    echo
    
    for i in {1..7}; do
        container_name="netchunk-ftp-$i"
        if docker ps --format "table {{.Names}}" | grep -q "$container_name"; then
            status="${GREEN}RUNNING${NC}"
            port="$((2120 + i))"
            user="netchunk$i"
        else
            status="${RED}STOPPED${NC}"
            port="N/A"
            user="N/A"
        fi
        
        printf "  Server %d: %b (Port: %s, User: %s)\n" "$i" "$status" "$port" "$user"
    done
    echo
}

show_server_info() {
    log_info "FTP Server Connection Information:"
    echo
    echo "Server | Host      | Port | Username   | Password  | Passive Ports"
    echo "-------|-----------|------|------------|-----------|---------------"
    echo "   1   | localhost | 2121 | netchunk1  | test123   | 21100-21110"
    echo "   2   | localhost | 2122 | netchunk2  | test456   | 21200-21210"  
    echo "   3   | localhost | 2123 | netchunk3  | test789   | 21300-21310"
    echo "   4   | localhost | 2124 | netchunk4  | secure456 | 21400-21410"
    echo "   5   | localhost | 2125 | netchunk5  | secure789 | 21500-21510"
    echo "   6   | localhost | 2126 | netchunk6  | admin123  | 21600-21610"
    echo "   7   | localhost | 2127 | netchunk7  | admin456  | 21700-21710"
    echo
}

test_connections() {
    log_info "Testing FTP server connections..."
    
    local failed_tests=0
    
    # Test each server connection
    for i in {1..7}; do
        port="$((2120 + i))"
        user="netchunk$i"
        
        case $i in
            1) pass="test123" ;;
            2) pass="test456" ;;
            3) pass="test789" ;;
            4) pass="secure456" ;;
            5) pass="secure789" ;;
            6) pass="admin123" ;;
            7) pass="admin456" ;;
        esac
        
        log_info "Testing server $i (localhost:$port)..."
        
        # Use curl to test FTP connection
        if timeout 10 curl -s --ftp-pasv -u "$user:$pass" "ftp://localhost:$port/" &> /dev/null; then
            log_success "Server $i connection test passed"
        else
            log_error "Server $i connection test failed"
            ((failed_tests++))
        fi
    done
    
    if [ $failed_tests -gt 0 ]; then
        log_error "$failed_tests connection tests failed"
        return 1
    fi
    
    log_success "All connection tests passed"
}

cleanup() {
    log_info "Cleaning up containers and volumes..."
    
    if docker compose version &> /dev/null; then
        docker compose -f "$COMPOSE_FILE" down -v
    else
        docker-compose -f "$COMPOSE_FILE" down -v
    fi
    
    # Remove any dangling containers
    for i in {1..7}; do
        if docker ps -aq -f "name=netchunk-ftp-$i" | grep -q .; then
            docker rm -f "netchunk-ftp-$i" || true
        fi
    done
    
    log_success "Cleanup completed"
}

show_logs() {
    local server_num=${1:-}
    
    if [ -z "$server_num" ]; then
        log_info "Showing logs for all FTP servers..."
        if docker compose version &> /dev/null; then
            docker compose -f "$COMPOSE_FILE" logs -f
        else
            docker-compose -f "$COMPOSE_FILE" logs -f
        fi
    else
        if [ "$server_num" -ge 1 ] && [ "$server_num" -le 7 ]; then
            log_info "Showing logs for FTP server $server_num..."
            docker logs -f "netchunk-ftp-$server_num"
        else
            log_error "Invalid server number. Use 1-7"
            exit 1
        fi
    fi
}

show_help() {
    echo "NetChunk FTP Test Server Management Script"
    echo
    echo "Usage: $0 <command> [options]"
    echo
    echo "Commands:"
    echo "  start       Start all FTP test servers"
    echo "  stop        Stop all FTP test servers"  
    echo "  restart     Restart all FTP test servers"
    echo "  status      Show status of all servers"
    echo "  info        Show server connection information"
    echo "  test        Test connections to all servers"
    echo "  logs [num]  Show logs (all servers or specific server 1-7)"
    echo "  cleanup     Stop containers and remove volumes"
    echo "  help        Show this help message"
    echo
    echo "Examples:"
    echo "  $0 start              # Start all servers"
    echo "  $0 test               # Test all connections"
    echo "  $0 logs 1             # Show logs for server 1"
    echo "  $0 logs               # Show logs for all servers"
    echo
}

# Main script logic
main() {
    check_docker
    
    case "${1:-help}" in
        start)
            start_servers
            ;;
        stop)
            stop_servers
            ;;
        restart)
            restart_servers
            ;;
        status)
            show_status
            ;;
        info)
            show_server_info
            ;;
        test)
            test_connections
            ;;
        logs)
            show_logs "$2"
            ;;
        cleanup)
            cleanup
            ;;
        help|--help|-h)
            show_help
            ;;
        *)
            log_error "Unknown command: $1"
            show_help
            exit 1
            ;;
    esac
}

main "$@"

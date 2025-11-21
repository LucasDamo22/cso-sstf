#!/bin/sh

# 1. Captura de Parâmetros (com valores padrão)
DEBUG_MODE=${1:-0}      # Padrão: 0 (Desligado)
QUEUE_SIZE=${2:-64}     # Padrão: 64           
MAX_WAIT=${3:-50}       # Padrão: 50ms

echo "======================================================="
echo " INICIANDO CONFIGURAÇÃO DO SSTF"
echo "  - Debug Mode:    $DEBUG_MODE"
echo "  - Queue Size:    $QUEUE_SIZE"
echo "  - Max Wait Time: ${MAX_WAIT}ms"
echo "======================================================="


if grep -q "\[sstf\]" /sys/block/sdb/queue/scheduler; then
    echo noop > /sys/block/sdb/queue/scheduler
fi

dmesg -c > /dev/null

rmmod sstf-iosched 2>/dev/null

if ! modprobe sstf-iosched queue_size=$QUEUE_SIZE debug_mode=$DEBUG_MODE max_wait_time=$MAX_WAIT; then
    echo "Falha ao carregar sstf-iosched via modprobe."
    exit 1
fi

if ! echo sstf > /sys/block/sdb/queue/scheduler; then
    echo "Não foi possível escrever em /sys/block/sdb/queue/scheduler"
    rmmod sstf-iosched
    exit 1
fi

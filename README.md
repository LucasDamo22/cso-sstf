# cso-sstf
- Config scripts
Ao entrar no QEMU, faça chmod +x /root/loadsstf.sh /root/runtests.sh
- Teste manual
Para carregar o modulo e fazer testes manuais, ./loadsstf.sh {debug} {queue_size} {max_time_wait}
exemplo: ./loadsstf.sh 1 64 50

Para rodar uma forma customizada da aplicação de teste:

/bin/sector_read -n {num_ops} -w {write_percentage} -p {num_procs} -m {min size of request} -M {max size of request}
- Teste automatizado
O teste automatizado roda so 5 casos teste presentes no relatório e busca no log do kernel os resultados e mostra no espaço de usuário
./runtests.sh


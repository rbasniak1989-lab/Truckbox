# Memória — Core 0.2.12 candidata

Comparação de dois ELFs compilados localmente para ESP32-C6 com ESP-IDF 5.5.2: baseline 0.2.11 (`8ee91d4`) e candidata 0.2.12. O perfil J1708_ONLY foi conferido no sdkconfig. `can_task`, `twai_driver_install` e `twai_driver_install_v2` não aparecem no ELF candidato; aparecem na baseline. O código J1939 continua no repositório e sua regressão nativa continua sendo executada.

| Seção física | 0.2.11 | 0.2.12 | Economia |
|---|---:|---:|---:|
| `.dram0.bss` | 75.104 B | 59.360 B | 15.744 B |
| `.dram0.data` | 14.788 B | 14.788 B | 0 B |
| `.iram0.data` | 4 B | 8 B | −4 B |
| `.iram0.text` | 128.956 B | 127.464 B | 1.492 B |
| Total estático em RAM | 218.852 B | 201.620 B | **17.232 B** |

Além disso, a redução conhecida de pedidos de alocação dinâmica é 10.240 B:

- Fila de aquisição de 128 para 64 itens de 104 B: 6.656 B.
- Fila RX TWAI de 128 itens de 20 B removida: 2.560 B.
- Pilha da tarefa CAN removida: 5.120 B.
- Pilha de armazenamento aumentada de 7.168 para 9.216 B: custo 2.048 B.
- Pilha da ponte aumentada de 8.192 para 10.240 B: custo 2.048 B.

Soma estática + alocações conhecidas: **27.472 B**. O cache passou de 192 para 128 observações; decoder e sessões TP J1939 não são alocados no perfil. Contextos do driver, semáforos, TCBs e overhead de malloc não entram nessa soma, para não apresentar precisão falsa. Os tamanhos 104 e 20 B foram lidos do DWARF dos ELFs com GDB.

Isto não é uma medição do heap livre na placa. Os campos `heap_free` e `heap_minimum`, comportamento sob carga e stack high-water continuam exigindo validação física.

O GCC gerou `.su` com `-fstack-usage`. A cadeia de recuperação conhecida do worker soma aproximadamente 5,4 KiB antes dos frames adicionais de libc/FAT/RTOS. A ponte chama os mesmos leitores e tem frame próprio maior; suas pilhas foram ampliadas para manter margem. Isso auxilia a revisão, sem substituir a medição de pilha no dispositivo.

Reproduzir após compilar os dois refs:

```sh
python tools/report_memory.py /caminho/baseline/truckbox_core.elf build-core/truckbox_core.elf
```

O script soma seções de RAM física. O total genérico `bss` do GNU size inclui seções dummy de mapeamento de flash neste target e não representa a RAM utilizada.

Evidência: `test_evidence/v0212_memory.json`. A tabela deve ser atualizada se o ELF for recompilado após mudanças em código ou configuração.

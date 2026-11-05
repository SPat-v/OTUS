    bits 64
    extern malloc, puts, printf, fflush, abort
    global main

    section   .data
empty_str: db 0x0               ;Резервирует 1 байт со значением 0 — пустая C-строка (нулевой терминатор).
                                ;Используется как аргумент puts для перевода строки.

int_format: db "%ld ", 0x0      ;Строка формата для printf: %ld — знаковое длинное целое (64 бит), 0x0 — терминатор
data: dq 4, 8, 15, 16, 23, 42   ; define quadword (8 байт). Массив из 6 чисел типа long (64-бит)
data_length: equ ($-data) / 8   ;$-data — размер массива в байтах, деление на 8 — количество элементов = 6

    section   .text
;;; print_int proc
print_int:              ;печать одного числа
    push rbp            ;Пролог : сохраняем rbp
    mov rbp, rsp        ;устанавливаем кадр стека,
    sub rsp, 16         ;выделяем 16 байт под локальные нужды и выравнивание стека (по ABI перед call rsp должен быть кратен 16).

    mov rsi, rdi        ;Аргумент пришёл в rdi (значение для печати). printf ждёт формат в rdi, значение — в rsi. Перекладываем.

    mov rdi, int_format ;Первый аргумент printf — указатель на строку формата.
    xor rax, rax        ;обнуляем rax
    call printf         ;вызов printf

    xor rdi, rdi        ;fflush(NULL) — сброс всех потоков вывода (чтобы вывод появился сразу, а не по завершении программы).
    call fflush         ;|

    mov rsp, rbp        ;Эпилог: восстанавливаем rsp/rbp, 
    pop rbp             ;|
    ret                 ;возврат

;;; p proc              предикат нечётности
p:
    mov rax, rdi        ;Копируем аргумент в rax
    and rax, 1          ;оставляет младший бит: 1 — нечётное, 0 — чётное. Результат (0/1) в rax
    ret                 ;возврат

;;; add_element proc
add_element:
    push rbp            ;Пролог с сохранением регистров rbx, r14 
    push rbx            ;|
    push r14            ;|
    mov rbp, rsp        ;|
    sub rsp, 16         ;выравнивание

    mov r14, rdi        ;rdi = value, rsi = next (старый список). Прячем в сохраняемые регистры, т.к. дальше будет call malloc, который может затереть rdi/rsi.
    mov rbx, rsi

    mov rdi, 16             ; размер узла = 16 байт
    call malloc
    test rax, rax           ;устанавливает ZF, если rax == 0. jz — переход, если ZF=1 (т.е. malloc вернул NULL) → аварийное завершение.
    jz abort

    mov [rax], r14          ; поле value (значение)по смещению 0
    mov [rax + 8], rbx      ; поле next  (указатель на следующий узел)по смещению 8

    mov rsp, rbp            ;Восстанавливаем регистры в обратном порядке пуша, возвращаем rax (указатель на узел).
    pop r14                 ;|
    pop rbx                 ;|
    pop rbp                 ;|
    ret                     ;|

;;; m proc                  ;обход списка (map/for_each): Второй вызов m с print_int печатает отфильтрованный список
m:
    push rbp                ;Пролог
    mov rbp, rsp
    sub rsp, 16

    test rdi, rdi           ;указатель на список. 
    jz outm                 ; если список пуст(NULL) — выходим

    push rbp                ;Здесь сохраняются rbp и rbx второй раз — это «вложенный» пролог внутри функции. 
    push rbx                ;Дальше используется rbp как хранилище fn, а rbx — как указатель на узел.

    mov rbx, rdi            ;rbx = текущий узел, rbp = функция обработки
    mov rbp, rsi

    mov rdi, [rdi]          ; берём value текущего узла
    call rsi                ; применяем функцию к значению

    mov rdi, [rbx + 8]      ; берём next
    mov rsi, rbp            ; передаём ту же функцию дальше
    call m                  ; рекурсивно идём по хвосту

    pop rbx                 ;Восстанавливаем сохранённые ранее rbx/rbp (пары из вложенного пролога)
    pop rbp

outm:   ;Эпилог. если был ранний выход (jz outm), то rbp — это тот, что сохранён в основном прологе rsp восстанавливается из него. 
        ;Работает за счёт того, что во вложенном прологе rbp перезаписывается, 
        ; но потом восстанавливается и mov rsp, rbp в outm использует «внешний» rbp.
    mov rsp, rbp
    pop rbp
    ret

;;; f proc                      ;фильтрация списка. Идёт по списку, применяет предикат p(x) = x & 1 (нечётность).
f:
    mov rax, rsi                ; acc — аккумулятор Кладём в rax аккумулятор acc (rsi) 

    test rdi, rdi
    jz outf                     ; конец списка → вернуть acc Если список пуст — возвращаем acc (уже в rax)

    push rbx                    ;Сохраняем регистры: rbx (узел), r12 (acc), r13 (pred)
    push r12
    push r13

    mov rbx, rdi                ;rbx = текущий узел, r12 = acc, r13 = pred.
    mov r12, rsi
    mov r13, rdx

    mov rdi, [rdi]              ; value
    call rdx                    ; pred(value) Результат в rax
    test rax, rax               ;Если предикат вернул 0 — идём в метку z 
    jz z                        ; предикат ложен → пропускаем 

    mov rdi, [rbx]              ; value
    mov rsi, r12                ; текущий acc
    call add_element            ; acc = add_element(value, acc)
    mov rsi, rax                ;Предикат истинен: add_element(value, acc) → новый acc в rax; переносим его в rsi (новый аккумулятор для рекурсии). jmp ff — обходим ветку z
    jmp ff

z:
    mov rsi, r12                ;Предикат ложен: acc не меняется

ff:                         ;rdi = next, rsi = acc, rdx = pred. Рекурсивный вызов для хвоста. Возврат из рекурсии — в rax
    mov rdi, [rbx + 8]          ; next
    mov rdx, r13                ; pred
    call f                      ; рекурсия по хвосту с новым acc
                
    pop r13                     ;Восстанавливаем регистры
    pop r12
    pop rbx

outf:
    ret                         ;Возврат. rax — результат (acc, накопленный в рекурсии)

;;; main proc
main:
    mov rbp, rsp                        ; for correct debugging
    push rbx                            ;Пролог + сохранение rbx (callee-saved). mov rbp, rsp

    xor rax, rax                        ; обнуляем rax rax = 0 — начальный указатель на список (пустой список = NULL)
    mov rbx, data_length                ;rbx = 6 — счётчик цикла


adding_loop:                        ;Цикл построения списка:

    mov rdi, [data - 8 + rbx * 8]       ;[data - 8 + rbx*8] — адрес элемента массива с индексом rbx-1 (при rbx=6 → data+40 → 42; при rbx=1 → data+0 → 4).
    mov rsi, rax                        ;rsi = rax — текущий список (аккумулятор)
    call add_element                    ;add_element(value, acc) → новый список в rax.
    dec rbx                             ;dec rbx — уменьшаем счётчик, jnz — повторяем, пока не 0.
    jnz adding_loop                     ;|
                        ;Итог: в rax список 4 → 8 → 15 → 16 → 23 → 42
    mov rbx, rax                        ;Прячем список в rbx (понадобится после m)

    mov rdi, rax                        ;m(list, print_int) — печать всех элементов
    mov rsi, print_int                  ;|
    call m                              ;|

    mov rdi, empty_str                  ;puts("") — печать пустой строки + \n (перевод строки)
    call puts

    mov rdx, p                          ;f(list, NULL, p) — фильтрация нечётных. Аргументы: rdi = список, rsi = 0 (пустой acc), rdx = p. Результат в rax
    xor rsi, rsi
    mov rdi, rbx
    call f

    mov rdi, rax                        ;m(filtered, print_int) — печать отфильтрованного списка.
    mov rsi, print_int
    call m

    mov rdi, empty_str                  ;Перевод строки.

    call puts

    pop rbx                             ;Восстанавливаем rbx

    xor rax, rax                        ;rax = 0 — код возврата main = EXIT_SUCCESS
    ret                                 ;возврат в CRT

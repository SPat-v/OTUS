    bits 64
    extern malloc, puts, printf, fflush, abort, free
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
    push rbp            ;Пролог функции: сохраняем rbp
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
    push rbp            ;Пролог с сохранением регистров rbx, r14 (их обязана сохранить вызываемая функция)
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

;;; m proc — обход списка (for_each)
;;; rdi = list, rsi = fn
;;; Использует: rbx (узел), r12 (fn)
m:
    m:
    push rbx
    push r12
    push r13               ; выравнивание (3 пуша: 8+24 = 32 ≡ 0 mod 16)
    mov  rbx, rdi
    mov  r12, rsi
.loop:
    test rbx, rbx
    jz   .done
    mov  rdi, [rbx]
    call r12
    mov  rbx, [rbx + 8]
    jmp  .loop
.done:
    pop  r13
    pop  r12
    pop  rbx
    ret


;;;; f proc — фильтрация списка
;;; rdi = list, rsi = acc, rdx = pred
;;; Возвращает: rax = новый список
f:
    push rbx
    push r12
    push r13
    sub  rsp, 8            ; выравнивание
    mov  rbx, rdi          ; rbx = узел
    mov  r12, rsi          ; r12 = acc
    mov  r13, rdx          ; r13 = pred

.loop:
    test rbx, rbx
    jz   .done             ; конец списка
    mov  rdi, [rbx]        ; value
    call r13               ; pred(value)
    test rax, rax
    jz   .skip             ; предикат ложен — acc не меняем
    mov  rdi, [rbx]        ; value
    mov  rsi, r12          ; acc
    call add_element       ; acc = add_element(value, acc)
    mov  r12, rax          ; обновляем acc
.skip:
    mov  rbx, [rbx + 8]    ; next
    jmp  .loop

.done:
    mov  rax, r12          ; результат = acc
    add  rsp, 8
    pop  r13
    pop  r12
    pop  rbx
    ret

;;; free_list proc
free_list:
    push rbx
    push r12
    sub  rsp, 8
    mov  rbx, rdi
.loop:
    test rbx, rbx
    jz   .done
    mov  r12, [rbx + 8]
    mov  rdi, rbx
    call free
    mov  rbx, r12
    jmp  .loop
.done:
    add  rsp, 8
    pop  r12
    pop  rbx
    ret

;;; main proc
main:
    push rbx
    push r14
    sub  rsp, 8

    xor  rax, rax
    mov  rbx, data_length
adding_loop:
    mov  rdi, [data - 8 + rbx * 8]
    mov  rsi, rax
    call add_element
    dec  rbx
    jnz  adding_loop
    mov  rbx, rax

    mov  rdi, rbx
    mov  rsi, print_int
    call m

    mov  rdi, empty_str
    call puts

    mov  rdx, p
    xor  rsi, rsi
    mov  rdi, rbx
    call f
    mov  r14, rax

    mov  rdi, r14
    mov  rsi, print_int
    call m

    mov  rdi, empty_str
    call puts

    mov  rdi, r14
    call free_list
    mov  rdi, rbx
    call free_list

    add  rsp, 8
    pop  r14
    pop  rbx

    xor  rax, rax
    ret
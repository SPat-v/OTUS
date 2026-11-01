/*
 * zipdet.c — поиск Zip-архива внутри файла и вывод списка содержимого.
 *
 * Платформа: Linux
 *
 * Использование:
 *   ./zipdet <файл>
 */
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Коды ошибок                                                        */
typedef enum
{
    /* Ошибки ввода-вывода */
    FILE_OK = 0,
    FILE_ERR_OPEN,    /* не удалось открыть файл          */
    FILE_ERR_FSTAT,   /* не удалось получить размер файла */
    FILE_ERR_READ,    /* ошибка чтения                    */
    FILE_ERR_BIG,     /* файл слишком большой             */
    MEM_ERR_NOMEM,    /* нехватка памяти                  */
    /* Ошибки формата Zip */
    ZIP_OK,
    ZIP_ERR_NO_EOCD,      /* сигнатура EOCD не найдена        */
    ZIP_ERR_MULTI_VOLUME, /* мульти-томный архив              */
    ZIP_ERR_BAD_CD,       /* Central Directory вне буфера     */
    ZIP_ERR_BAD_CFH,      /* не удалось прочитать CFH         */
    ZIP_ERR_BAD_LFH,      /* не удалось прочитать LFH         */
    ZIP_ERR_NOT_FOUND,    /* Zip-архив не найден              */
    /* Ошибки использования */
    ZIP_ERR_USAGE,    /* неверные аргументы командной строки */
} zip_status_t;

/* Константы Zip    */

#define SIG_LFH  0x04034b50u  /* "PK\3\4" — Local File Header        */
#define SIG_CFH  0x02014b50u  /* "PK\1\2" — Central File Header      */
#define SIG_EOCD 0x06054b50u  /* "PK\5\6" — End of Central Directory */

#define LFH_BASE_SZ  30   /* мин. длина LFH без имени/extra      */
#define CFH_BASE_SZ  46   /* мин. длина CFH без имени/extra/комм.*/
#define EOCD_BASE_SZ 22   /* мин. длина EOCD без комментария     */

/* Максимальная длина комментария: на сколько байт от конца файла
 * может отстоять EOCD. 65535 (комментарий) + 22 (EOCD). */
#define MAX_EOCD_SEARCH 65557

/* Структуры Zip              */

struct eocd
{
    uint16_t disk_nbr;
    uint16_t cd_start_disk;
    uint16_t disk_cd_entries;
    uint16_t cd_entries;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
    const uint8_t *comment;
    size_t offset_in_file;
};

struct cfh
{
    uint16_t made_by_ver;
    uint16_t extract_ver;
    uint16_t gp_flag;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    uint16_t comment_len;
    uint16_t disk_nbr_start;
    uint16_t int_attrs;
    uint32_t ext_attrs;
    uint32_t lfh_offset;
    const uint8_t *name;
    const uint8_t *extra;
    const uint8_t *comment;
};

struct lfh
{
    uint16_t extract_ver;
    uint16_t gp_flag;
    uint16_t method;
    uint16_t mod_time;
    uint16_t mod_date;
    uint32_t crc32;
    uint32_t comp_size;
    uint32_t uncomp_size;
    uint16_t name_len;
    uint16_t extra_len;
    const uint8_t *name;
    const uint8_t *extra;
};

/* функции          */

/* считать 16 бит с порядком байтов little-endian и продвинуть указатель *p. */
static uint16_t read16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}
/* считать 32 бита с порядком байтов little-endian и продвинуть указатель *p. */
static uint32_t read32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Чтение файла       */

/* Читает файл до конца. При успехе — ZIP_OK, *out_buf / *out_len.
 * При ошибке — соответствующий код; причина errno — в *out_errno. */
static zip_status_t read_file(const char *path, uint8_t **out_buf,
                              size_t *out_len, int *out_errno)
{
    *out_buf = NULL;
    *out_len = 0;
    if (out_errno != NULL) {
        *out_errno = 0;
    }

    int fd = open(path, O_RDONLY);
    if (fd < 0) {
        if (out_errno) *out_errno = errno;
        return FILE_ERR_OPEN;
    }

    struct stat st;
    if (fstat(fd, &st) != 0) {
        if (out_errno) *out_errno = errno;
        close(fd);
        return FILE_ERR_FSTAT;
    }

    size_t cap = (st.st_size > 0) ? (size_t)st.st_size : 65536;
    size_t len = 0;

    uint8_t *buf = malloc(cap);
    if (buf == NULL) {
        close(fd);
        return MEM_ERR_NOMEM;
    }

    for (;;) {
        if (len == cap) {
            if (cap > SIZE_MAX / 2) {
                free(buf);
                close(fd);
                return FILE_ERR_BIG;
            }
            cap *= 2;

            uint8_t *nb = realloc(buf, cap);
            if (nb == NULL) {
                free(buf);
                close(fd);
                return MEM_ERR_NOMEM;
            }
            buf = nb;
        }

        ssize_t r = read(fd, buf + len, cap - len);
        if (r < 0) {
            if (errno == EINTR) {
                continue;
            }
            if (out_errno) *out_errno = errno;
            free(buf);
            close(fd);
            return FILE_ERR_READ;
        }
        if (r == 0) {
            break; /* EOF */
        }
        len += (size_t)r;
    }

    close(fd);
    *out_buf = buf;
    *out_len = len;
    return FILE_OK;
}

/* Разбор EOCD            */
static zip_status_t find_eocd(struct eocd *r, const uint8_t *src,
                              size_t src_len)
{
    if (src_len < EOCD_BASE_SZ) {
        return ZIP_ERR_NO_EOCD;
    }

    size_t max_back = src_len - EOCD_BASE_SZ;
    if (max_back > MAX_EOCD_SEARCH) {
        max_back = MAX_EOCD_SEARCH;
    }

    for (size_t back = 0; back <= max_back; back++) {
        size_t off = src_len - EOCD_BASE_SZ - back;
        const uint8_t *p = src + off;

        if (read32le(p) != SIG_EOCD) {
            continue;
        }

        r->disk_nbr        = read16le(p + 4);
        r->cd_start_disk   = read16le(p + 6);
        r->disk_cd_entries = read16le(p + 8);
        r->cd_entries      = read16le(p + 10);
        r->cd_size         = read32le(p + 12);
        r->cd_offset       = read32le(p + 16);
        r->comment_len     = read16le(p + 20);
        r->comment         = p + EOCD_BASE_SZ;
        r->offset_in_file  = off;

        /* Комментарий должен умещаться в буфер. */
        if ((size_t)r->comment_len > back) {
            continue;
        }
        return ZIP_OK;
    }

    return ZIP_ERR_NO_EOCD;
}

/* Разбор CFH              */
static zip_status_t read_cfh(struct cfh *cfh, const uint8_t *src,
                             size_t src_len, size_t offset)
{
    if (offset > src_len || src_len - offset < CFH_BASE_SZ) {
        return ZIP_ERR_BAD_CFH;
    }

    const uint8_t *p = src + offset;
    if (read32le(p) != SIG_CFH) {
        return ZIP_ERR_BAD_CFH;
    }

    cfh->made_by_ver    = read16le(p + 4);
    cfh->extract_ver    = read16le(p + 6);
    cfh->gp_flag        = read16le(p + 8);
    cfh->method         = read16le(p + 10);
    cfh->mod_time       = read16le(p + 12);
    cfh->mod_date       = read16le(p + 14);
    cfh->crc32          = read32le(p + 16);
    cfh->comp_size      = read32le(p + 20);
    cfh->uncomp_size    = read32le(p + 24);
    cfh->name_len       = read16le(p + 28);
    cfh->extra_len      = read16le(p + 30);
    cfh->comment_len    = read16le(p + 32);
    cfh->disk_nbr_start = read16le(p + 34);
    cfh->int_attrs      = read16le(p + 36);
    cfh->ext_attrs      = read32le(p + 38);
    cfh->lfh_offset     = read32le(p + 42);
    cfh->name           = p + CFH_BASE_SZ;
    cfh->extra          = cfh->name + cfh->name_len;
    cfh->comment        = cfh->extra + cfh->extra_len;

    size_t need = (size_t)CFH_BASE_SZ + cfh->name_len
                + cfh->extra_len + cfh->comment_len;
    if (src_len - offset < need) {
        return ZIP_ERR_BAD_CFH;
    }
    return ZIP_OK;
}
/* Разбор LFH             */
static zip_status_t read_lfh(struct lfh *lfh, const uint8_t *src,
                             size_t src_len, size_t offset)
{
    if (offset > src_len || src_len - offset < LFH_BASE_SZ) {
        return ZIP_ERR_BAD_LFH;
    }

    const uint8_t *p = src + offset;
    if (read32le(p) != SIG_LFH) {
        return ZIP_ERR_BAD_LFH;
    }

    lfh->extract_ver = read16le(p + 4);
    lfh->gp_flag     = read16le(p + 6);
    lfh->method      = read16le(p + 8);
    lfh->mod_time    = read16le(p + 10);
    lfh->mod_date    = read16le(p + 12);
    lfh->crc32       = read32le(p + 14);
    lfh->comp_size   = read32le(p + 18);
    lfh->uncomp_size = read32le(p + 22);
    lfh->name_len    = read16le(p + 26);
    lfh->extra_len   = read16le(p + 28);
    lfh->name        = p + LFH_BASE_SZ;
    lfh->extra       = lfh->name + lfh->name_len;

    size_t need = (size_t)LFH_BASE_SZ + lfh->name_len + lfh->extra_len;
    if (src_len - offset < need) {
        return ZIP_ERR_BAD_LFH;
    }
    return ZIP_OK;
}

/* Проверка архива по базовому смещению      */

static zip_status_t try_zip_at(const uint8_t *data, size_t len, size_t base,
                               struct eocd *out_eocd)
{
    if (base > len) {
        return ZIP_ERR_NO_EOCD;
    }

    struct eocd e;
    zip_status_t rc = find_eocd(&e, data + base, len - base);
    if (rc != ZIP_OK) {
        return rc;
    }

    /* Мульти-томные архивы не поддерживаются. */
    if (e.disk_nbr != 0 || e.cd_start_disk != 0 ||
        e.disk_cd_entries != e.cd_entries) {
        return ZIP_ERR_MULTI_VOLUME;
    }

    /* Central Directory должна умещаться в буфер. */
    size_t cd_off = base + e.cd_offset;
    if (cd_off > len || len - cd_off < e.cd_size) {
        return ZIP_ERR_BAD_CD;
    }

    /* Проверяем все CFH и соответствующие LFH. */
    size_t off = cd_off;
    for (uint16_t i = 0; i < e.cd_entries; i++) {
        struct cfh cfh;
        rc = read_cfh(&cfh, data, len, off);
        if (rc != ZIP_OK) {
            return rc;
        }

        struct lfh lfh;
        rc = read_lfh(&lfh, data, len, base + cfh.lfh_offset);
        if (rc != ZIP_OK) {
            return rc;
        }

        off += (size_t)CFH_BASE_SZ + cfh.name_len
             + cfh.extra_len + cfh.comment_len;
    }

    *out_eocd = e;
    return ZIP_OK;
}

/* ------------------------------------------------------------------ */
/* Поиск архива                                                       */
/* ------------------------------------------------------------------ */

static zip_status_t find_zip(const uint8_t *data, size_t len,
                             struct eocd *out_eocd, size_t *out_base)
{
    if (len < EOCD_BASE_SZ) {
        return ZIP_ERR_NO_EOCD;
    }

    zip_status_t last_err = ZIP_ERR_NO_EOCD;

    /* 1. Быстрый путь: ищем EOCD с конца и для каждого пробуем
     *    разные базы. */
    size_t max_back = len - EOCD_BASE_SZ;
    if (max_back > MAX_EOCD_SEARCH) {
        max_back = MAX_EOCD_SEARCH;
    }

    for (size_t back = 0; back <= max_back; back++) {
        size_t eocd_off = len - EOCD_BASE_SZ - back;
        if (read32le(data + eocd_off) != SIG_EOCD) {
            continue;
        }

        for (size_t base = 0; base <= eocd_off; base++) {
            struct eocd e;
            zip_status_t rc = try_zip_at(data, len, base, &e);
            switch (rc) {
            case ZIP_OK:
                *out_eocd = e;
                *out_base = base;
                return ZIP_OK;

            case ZIP_ERR_MULTI_VOLUME:
            case ZIP_ERR_BAD_CD:
            case ZIP_ERR_BAD_CFH:
            case ZIP_ERR_BAD_LFH:
                last_err = rc;
                break;

            case ZIP_ERR_NO_EOCD:
            default:
                break;
            }
        }
    }

    /* 2. Запасной путь: полный перебор base. */
    for (size_t base = 0; base + EOCD_BASE_SZ <= len; base++) {
        struct eocd e;
        zip_status_t rc = try_zip_at(data, len, base, &e);
        switch (rc) {
        case ZIP_OK:
            *out_eocd = e;
            *out_base = base;
            return ZIP_OK;

        case ZIP_ERR_MULTI_VOLUME:
        case ZIP_ERR_BAD_CD:
        case ZIP_ERR_BAD_CFH:
        case ZIP_ERR_BAD_LFH:
            last_err = rc;
            break;

        case ZIP_ERR_NO_EOCD:
        default:
            break;
        }
    }

    return last_err;
}

/* Вывод                            */
/* выводим обнаруженный архив и коментарий - если есть*/
static void print_member(const struct cfh *cfh)
{
    printf("  Имя:        %.*s%s\n",
           (int)cfh->name_len, (const char *)cfh->name,
           (cfh->ext_attrs & 0x10) ? "/" : "");

    if (cfh->comment_len > 0) {
        printf("  Комментарий: %.*s\n",
               (int)cfh->comment_len, (const char *)cfh->comment);
    }
}

/* Обработка кодов ошибок               */

static void report_error(zip_status_t err, const char *path, int saved_errno)
{
    switch (err) {
    case ZIP_OK:
        break;

    case FILE_ERR_OPEN:
        fprintf(stderr, "Ошибка: не удалось открыть '%s': %s\n",
                path, strerror(saved_errno));
        break;

    case FILE_ERR_FSTAT:
        fprintf(stderr, "Ошибка: не удалось получить размер '%s': %s\n",
                path, strerror(saved_errno));
        break;

    case FILE_ERR_READ:
        fprintf(stderr, "Ошибка: не удалось прочитать '%s': %s\n",
                path, strerror(saved_errno));
        break;

    case MEM_ERR_NOMEM:
        fprintf(stderr, "Ошибка: не хватает памяти.\n");
        break;

    case FILE_ERR_BIG:
        fprintf(stderr, "Ошибка: файл слишком большой.\n");
        break;

    case ZIP_ERR_NO_EOCD:
        fprintf(stderr, "Zip-архив не найден (нет сигнатуры EOCD).\n");
        break;

    case ZIP_ERR_NOT_FOUND:
        fprintf(stderr, "Zip-архив не найден.\n");
        break;

    case ZIP_ERR_MULTI_VOLUME:
        fprintf(stderr, "Ошибка: мульти-томные архивы не поддерживаются.\n");
        break;

    case ZIP_ERR_BAD_CD:
        fprintf(stderr, "Ошибка: Central Directory выходит за границы файла.\n");
        break;

    case ZIP_ERR_BAD_CFH:
        fprintf(stderr, "Ошибка: повреждён Central File Header.\n");
        break;

    case ZIP_ERR_BAD_LFH:
        fprintf(stderr, "Ошибка: повреждён Local File Header.\n");
        break;

    case ZIP_ERR_USAGE:
        fprintf(stderr, "Ошибка: неверные аргументы.\n"
                        "Использование: zipdet <файл>\n");
        break;

    default:
        fprintf(stderr, "Ошибка: неизвестный код %d.\n", (int)err);
        break;
    }
}

/* main                   */

int main(int argc, char **argv)
{
    zip_status_t rc;
    int exit_code = EXIT_FAILURE;
    uint8_t *file_data = NULL;
    size_t file_len = 0;
    int saved_errno = 0;
    struct eocd eocd;
    size_t base = 0;

    if (argc != 2) {
        report_error(ZIP_ERR_USAGE, NULL, 0);
        return EXIT_FAILURE;
    }

    const char *path = argv[1];

    /* 1. Чтение файла. */
    rc = read_file(path, &file_data, &file_len, &saved_errno);
    switch (rc) {
    case FILE_OK:
        break;

    case FILE_ERR_OPEN:
    case FILE_ERR_FSTAT:
    case FILE_ERR_READ:
    case MEM_ERR_NOMEM:
    case FILE_ERR_BIG:
        report_error(rc, path, saved_errno);
        goto cleanup;

    default:
        report_error(rc, path, saved_errno);
        goto cleanup;
    }

    printf("Файл: %s (%zu байт)\n", path, file_len);

    /* 2. Поиск Zip-архива. */
    rc = find_zip(file_data, file_len, &eocd, &base);
    switch (rc) {
    case ZIP_OK:
        break;

    case ZIP_ERR_NO_EOCD:
        puts("Zip-архив не найден.");
        goto cleanup;

    case ZIP_ERR_MULTI_VOLUME:
    case ZIP_ERR_BAD_CD:
    case ZIP_ERR_BAD_CFH:
    case ZIP_ERR_BAD_LFH:
        report_error(rc, path, 0);
        goto cleanup;

    default:
        report_error(rc, path, 0);
        goto cleanup;
    }

    /* 3. Печать информации об архиве. */
    printf("Найден Zip-архив по смещению %zu (размер ~%zu байт).\n",
           base + eocd.offset_in_file, file_len - base);

    if (eocd.comment_len > 0) {
        printf("Комментарий архива: %.*s\n",
               (int)eocd.comment_len, (const char *)eocd.comment);
    }
    printf("Элементов: %u\n\n", eocd.cd_entries);

    /* 4. Перебор Central Directory. */
    size_t off = base + eocd.cd_offset;
    for (uint16_t i = 0; i < eocd.cd_entries; i++) {
        struct cfh cfh;
        rc = read_cfh(&cfh, file_data, file_len, off);
        switch (rc) {
        case ZIP_OK:
            break;

        case ZIP_ERR_BAD_CFH:
            fprintf(stderr, "Ошибка: не удалось прочитать CFH #%u.\n", i);
            goto cleanup;

        default:
            report_error(rc, path, 0);
            goto cleanup;
        }

        print_member(&cfh);

        off += (size_t)CFH_BASE_SZ + cfh.name_len
             + cfh.extra_len + cfh.comment_len;
    }

    exit_code = EXIT_SUCCESS;

cleanup:
    free(file_data);
    return exit_code;
}

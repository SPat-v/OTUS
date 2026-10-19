/*
 * zipdet.c — поиск Zip-архива внутри файла и вывод списка содержимого.
 *
 * Платформа: Linux 
 *
 * Использование:
 *   ./zipdet <файл>
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>

/* -------------- */
/* Константы Zip  */
/* ---------------*/

#define SIG_LFH 0x04034b50u  /* "PK\3\4" — Local File Header - сигнатура файла                              */
#define SIG_CFH 0x02014b50u  /* "PK\1\2" — Central File Header - сигнатура файла в центральной директории   */
#define SIG_EOCD 0x06054b50u /* "PK\5\6" — End of Central Directory - сигнатура конца центральной директории*/
// #define SIG_DD    0x08074b50u  /* "PK\7\8" — Data Descriptor (не исп.) */

#define LFH_BASE_SZ 30  // мин длинна CLH
#define CFH_BASE_SZ 46  // мин длинна CFH без учета коментария
#define EOCD_BASE_SZ 22 // мин длинна EOCD без учета коментария

/* Максимальная длина коментария, на сколько байт от конца файла может отстоять EOCD */
#define MAX_EOCD_SEARCH 65557

/* функции          */

/* Выделение памяти */
static void *xmalloc(size_t n)
{
    void *p = malloc(n);
    if (p == NULL)
    {
        fprintf(stderr, "Ошибка: malloc(%zu): %s\n", n, strerror(errno));
        exit(EXIT_FAILURE);
    }
    return p;
}

/* считать 16 бита с порядком байтов little-endian и продвинуть указатель *p. */
static uint16_t read16le(const uint8_t *p)
{
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/* считать 32 бита с порядком байтов little-endian и продвинуть указатель *p. */
static uint32_t read32le(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* Чтение файла до конца. Возвращает буфер (malloc) и длину.
 * При ошибке возвращает NULL. */
static uint8_t *read_file(const char *path, size_t *out_len)
{
    int fd = open(path, O_RDONLY);
    if (fd < 0)
    {
        fprintf(stderr, "Ошибка: не удалось открыть '%s': %s\n",
                path, strerror(errno));
        return NULL;
    }

    struct stat st;
    if (fstat(fd, &st) != 0)
    {
        fprintf(stderr, "Ошибка: fstat('%s'): %s\n", path, strerror(errno));
        close(fd);
        return NULL;
    }

    size_t cap = (st.st_size > 0) ? (size_t)st.st_size : 65536;
    size_t len = 0;
    uint8_t *buf = xmalloc(cap);

    for (;;)
    {
        if (len == cap)
        {
            if (cap > SIZE_MAX / 2)
            {
                fprintf(stderr, "Ошибка: файл слишком большой.\n");
                free(buf);
                close(fd);
                return NULL;
            }
            cap *= 2;
            uint8_t *nb = realloc(buf, cap);
            if (nb == NULL)
            {
                fprintf(stderr, "Ошибка: realloc: %s\n", strerror(errno));
                free(buf);
                close(fd);
                return NULL;
            }
            buf = nb;
        }

        ssize_t r = read(fd, buf + len, cap - len);
        if (r < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            fprintf(stderr, "Ошибка: read('%s'): %s\n",
                    path, strerror(errno));
            free(buf);
            close(fd);
            return NULL;
        }
        if (r == 0)
        {
            break; /* EOF */
        }
        len += (size_t)r;
    }

    close(fd);
    *out_len = len;
    return buf;
}

/* структуры Zip   */

struct eocd
{
    uint16_t disk_nbr;
    uint16_t cd_start_disk;
    uint16_t disk_cd_entries;
    uint16_t cd_entries;
    uint32_t cd_size;
    uint32_t cd_offset;
    uint16_t comment_len;
    const uint8_t *comment; /* указатель внутрь буфера */
    size_t offset_in_file;  /* смещение EOCD в файле */
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

/* Ищет конец центральной директории EOCD с конца буфера. Возвращает true и заполняет r. */
static bool find_eocd(struct eocd *r, const uint8_t *src, size_t src_len)
{
    if (src_len < EOCD_BASE_SZ)
    {
        return false;
    }

    size_t max_back = src_len - EOCD_BASE_SZ;
    if (max_back > MAX_EOCD_SEARCH)
    {
        max_back = MAX_EOCD_SEARCH;
    }

    for (size_t back = 0; back <= max_back; back++)
    {
        size_t off = src_len - EOCD_BASE_SZ - back;
        const uint8_t *p = src + off;

        if (read32le(p) != SIG_EOCD)
        {
            continue;
        }

        r->disk_nbr = read16le(p + 4); //
        r->cd_start_disk = read16le(p + 6);
        r->disk_cd_entries = read16le(p + 8);
        r->cd_entries = read16le(p + 10);
        r->cd_size = read32le(p + 12);
        r->cd_offset = read32le(p + 16);
        r->comment_len = read16le(p + 20);
        r->comment = p + EOCD_BASE_SZ;
        r->offset_in_file = off;

        /* Комментарий должен умещаться в файл. */
        if ((size_t)r->comment_len > back)
        {
            continue;
        }
        return true;
    }

    return false;
}

/* Читает CFH по смещению offset. */
static bool read_cfh(struct cfh *cfh, const uint8_t *src, size_t src_len,
                     size_t offset)
{
    if (offset > src_len || src_len - offset < CFH_BASE_SZ)
    {
        return false;
    }

    const uint8_t *p = src + offset;
    if (read32le(p) != SIG_CFH)
    {
        return false;
    }

    cfh->made_by_ver = read16le(p + 4);
    cfh->extract_ver = read16le(p + 6);
    cfh->gp_flag = read16le(p + 8);
    cfh->method = read16le(p + 10);
    cfh->mod_time = read16le(p + 12);
    cfh->mod_date = read16le(p + 14);
    cfh->crc32 = read32le(p + 16);
    cfh->comp_size = read32le(p + 20);
    cfh->uncomp_size = read32le(p + 24);
    cfh->name_len = read16le(p + 28);
    cfh->extra_len = read16le(p + 30);
    cfh->comment_len = read16le(p + 32);
    cfh->disk_nbr_start = read16le(p + 34);
    cfh->int_attrs = read16le(p + 36);
    cfh->ext_attrs = read32le(p + 38);
    cfh->lfh_offset = read32le(p + 42);
    cfh->name = p + CFH_BASE_SZ;
    cfh->extra = cfh->name + cfh->name_len;
    cfh->comment = cfh->extra + cfh->extra_len;

    size_t need = (size_t)CFH_BASE_SZ + cfh->name_len + cfh->extra_len + cfh->comment_len;
    if (src_len - offset < need)
    {
        return false;
    }
    return true;
}

/* Читает LFH по смещению offset. */
static bool read_lfh(struct lfh *lfh, const uint8_t *src, size_t src_len,
                     size_t offset)
{
    if (offset > src_len || src_len - offset < LFH_BASE_SZ)
    {
        return false;
    }

    const uint8_t *p = src + offset;
    if (read32le(p) != SIG_LFH)
    {
        return false;
    }

    lfh->extract_ver = read16le(p + 4);
    lfh->gp_flag = read16le(p + 6);
    lfh->method = read16le(p + 8);
    lfh->mod_time = read16le(p + 10);
    lfh->mod_date = read16le(p + 12);
    lfh->crc32 = read32le(p + 14);
    lfh->comp_size = read32le(p + 18);
    lfh->uncomp_size = read32le(p + 22);
    lfh->name_len = read16le(p + 26);
    lfh->extra_len = read16le(p + 28);
    lfh->name = p + LFH_BASE_SZ;
    lfh->extra = lfh->name + lfh->name_len;

    size_t need = (size_t)LFH_BASE_SZ + lfh->name_len + lfh->extra_len;
    if (src_len - offset < need)
    {
        return false;
    }
    return true;
}

/* Вывод    */

/* выводим обнаруженный архив и коментарий - если есть*/
static void print_member(const struct cfh *cfh)
{
    printf("  Имя:        %.*s%s\n",
           (int)cfh->name_len, (const char *)cfh->name,
           (cfh->ext_attrs & 0x10) ? "/" : "");

  
    if (cfh->comment_len > 0)
    {
        printf("  Комментарий: %.*s\n",
               (int)cfh->comment_len, (const char *)cfh->comment);
    }
   // printf("\n");
}

/* Проверяем, что данные — валидный Zip, начиная с offset             */

/* Пробуем прочитать EOCD и Central Directory так, чтобы
 * смещения были согласованы. base — смещение начала архива в буфере. */
static bool try_zip_at(const uint8_t *data, size_t len, size_t base,
                       struct eocd *out_eocd, size_t *out_base)
{
    if (base > len)
    {
        return false;
    }

    struct eocd e;
    if (!find_eocd(&e, data + base, len - base))
    {
        return false;
    }

    /* Мульти-томные архивы не поддерживаем. */ 
    
    if (e.disk_nbr != 0 || e.cd_start_disk != 0 ||
        e.disk_cd_entries != e.cd_entries)
    {
        return false;
    }

    /* Проверяем, что Central Directory умещается в буфер. */
    size_t cd_off = base + e.cd_offset;
    if (cd_off > len || len - cd_off < e.cd_size)
    {
        return false;
    }

    /* Проверяем, что все CFH читаются. */
    size_t off = cd_off;
    for (uint16_t i = 0; i < e.cd_entries; i++)
    {
        struct cfh cfh;
        if (!read_cfh(&cfh, data, len, off))
        {
            return false;
        }
        /* Проверяем LFH по указанному смещению. */
        struct lfh lfh;
        if (!read_lfh(&lfh, data, len, base + cfh.lfh_offset))
        {
            return false;
        }
        off += (size_t)CFH_BASE_SZ + cfh.name_len + cfh.extra_len + cfh.comment_len;
    }

    *out_eocd = e;
    *out_base = base;
    return true;
}

/* Ищет Zip-архив в буфере. в случае удачи возвращает true и заполняет base. */
static bool find_zip(const uint8_t *data, size_t len,
                     struct eocd *out_eocd, size_t *out_base)
{
    if (len < EOCD_BASE_SZ)
    {
        return false;
    }

    /* Ищем все EOCD с конца и для каждого пробуем разные базы. */
    for (size_t back = 0; back <= len - EOCD_BASE_SZ &&
                          back <= MAX_EOCD_SEARCH;
         back++)
    {
        size_t eocd_off = len - EOCD_BASE_SZ - back;
        if (read32le(data + eocd_off) != SIG_EOCD)
        {
            continue;
        }

        /* Пробуем базу от 0 до eocd_off. Архив начинается либо с 0 (обычный .zip), либо
         * с какого-то смещения. Перебираем. */
        for (size_t base = 0; base <= eocd_off; base++)
        {
            struct eocd e;
            size_t b;
            if (try_zip_at(data, len, base, &e, &b))
            {
                *out_eocd = e;
                *out_base = b;
                return true;
            }
        }
    }

    /* Запасной вариант — на случай, если EOCD не находится из-за сдвига: полный перебор base. */
    for (size_t base = 0; base + EOCD_BASE_SZ <= len; base++)
    {
        struct eocd e;
        size_t b;
        if (try_zip_at(data, len, base, &e, &b))
        {
            *out_eocd = e;
            *out_base = b;
            return true;
        }
    }

    return false;
}

/* ------------------ */
/* main               */
/* -------------------*/

int main(int argc, char **argv)
{
     if (argc != 2) {
         fprintf(stderr, "Использование: %s <файл>\n", argv[0]);
         return EXIT_FAILURE;
     }
  
    const char *path =  argv[1];

    size_t file_len = 0;
    uint8_t *file_data = read_file(path, &file_len);
    if (file_data == NULL)
    {
        return EXIT_FAILURE;
    }

    printf("Файл: %s (%zu байт)\n", path, file_len);

    struct eocd eocd;
    size_t base = 0;
    if (!find_zip(file_data, file_len, &eocd, &base))
    {
        printf("Zip-архив не найден.\n");
        free(file_data);
        return EXIT_FAILURE;
    }

    printf("Найден Zip-архив по смещению %zu ", eocd.offset_in_file);

    if (eocd.comment_len > 0)
    {
        printf("Комментарий архива: %.*s\n", 
               (int)eocd.comment_len, (const char *)eocd.comment);
    }
    printf("Элементов: %u\n\n", eocd.cd_entries);

    /* Перебираем Central Directory. */
    size_t off = base + eocd.cd_offset;
    for (uint16_t i = 0; i < eocd.cd_entries; i++)
    {
        struct cfh cfh;
        if (!read_cfh(&cfh, file_data, file_len, off))
        {
            fprintf(stderr, "Ошибка: не удалось прочитать CFH #%u.\n", i);
            break;
        }

        print_member(&cfh);

        off += (size_t)CFH_BASE_SZ + cfh.name_len + cfh.extra_len + cfh.comment_len;
    }

    free(file_data);
    return EXIT_SUCCESS;
}
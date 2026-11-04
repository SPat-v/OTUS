#include <curl/curl.h>
#include <json-c/json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Структура для накопления ответа от сервера
struct MemoryStruct
{
    char *memory;
    size_t size;
};

// Функция обратного вызова для записи полученных данных в память
static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp)
{
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr)
    {
        fprintf(stderr, "Ошибка: не удалось выделить память.\n");
        return 0;
    }

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}
// curl
// Функция для выполнения HTTP-запроса и получения строки с JSON
char *receiv_weather_curl(const char *city)
{
    CURL *curl_handle;
    CURLcode res;
    long http_code = 0;
    struct MemoryStruct chunk;

    chunk.memory = malloc(1); // будет увеличен по мере необходимости
    chunk.size = 0;
    /* Глобальная инициализация libcurl  */
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl_handle = curl_easy_init();

    if (curl_handle)
    {
        // Формируем URL: wttr.in/<city>?format=j2
        char url[256];
        snprintf(url, sizeof(url), "wttr.in/%s?format=j2", city);

        curl_easy_setopt(curl_handle, CURLOPT_URL, url);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
        curl_easy_setopt(curl_handle, CURLOPT_WRITEDATA, (void *)&chunk);
        //       curl_easy_setopt(curl_handle, CURLOPT_USERAGENT, "libcurl-agent/1.0");

        /* Таймауты */
        curl_easy_setopt(curl_handle, CURLOPT_CONNECTTIMEOUT, 20L); /* 10 сек на соединение */
        curl_easy_setopt(curl_handle, CURLOPT_TIMEOUT, 40L);        /* 30 сек на весь запрос */

        /* Следование за редиректами */
        curl_easy_setopt(curl_handle, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_MAXREDIRS, 5L);

        /* Проверка TLS-сертификата  */
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYPEER, 1L);
        curl_easy_setopt(curl_handle, CURLOPT_SSL_VERIFYHOST, 2L);

        // Выполняем запрос
        res = curl_easy_perform(curl_handle);

        /* Получаем HTTP-код ответа (даже если запрос не удался) */
        curl_easy_getinfo(curl_handle, CURLINFO_RESPONSE_CODE, &http_code);

        if (res != CURLE_OK)
        {
            fprintf(stderr, "Ошибка curl_easy_perform(): %s\n", curl_easy_strerror(res));

            /* типичные ошибки */
            switch (res)
            {
            case CURLE_OPERATION_TIMEDOUT:
                fprintf(stderr, "  Причина: превышен таймаут (соединение или весь запрос).\n");
                break;
            case CURLE_COULDNT_RESOLVE_HOST:
                fprintf(stderr, "  Причина: не удалось разрешить имя хоста.\n");
                break;
            case CURLE_COULDNT_CONNECT:
                fprintf(stderr, "  Причина: не удалось установить соединение.\n");
                break;
            case CURLE_SSL_CONNECT_ERROR:
                fprintf(stderr, "  Причина: ошибка TLS/SSL.\n");
                break;
            default:
                break;
            }
            free(chunk.memory);
            chunk.memory = NULL;
        }

        curl_easy_cleanup(curl_handle);
    }

    /* Разбор HTTP-кода */

   // printf("HTTP-код ответа: %ld\n", http_code);

    if (http_code >= 200 && http_code < 300)
    {
        /* выводим полученные данные */
        //printf("Размер полученных данных: %zu байт\n\n", chunk.size);
        /* Выводим ответ */
          // fwrite(chunk.memory, 1, chunk.size, stdout);
    }
    else if (http_code >= 300 && http_code < 400)
    {
        fprintf(stderr, "Перенаправление (код %ld), не сработал.\n", http_code);
    }
    else if (http_code >= 400 && http_code < 500)
    {
        fprintf(stderr, "Клиентская ошибка (код %ld). Проверьте URL/город.\n", http_code);
        if (chunk.size > 0)
        {
            fwrite(chunk.memory, 1, chunk.size, stderr);
            fputc('\n', stderr);
        }
    }
    else if (http_code >= 500)
    {
        fprintf(stderr, "Ошибка сервера (код %ld). Попробуйте позже.\n", http_code);
    }
    else
    {
        fprintf(stderr, "Неожиданный HTTP-код: %ld\n", http_code);
    }

    curl_global_cleanup();
    return chunk.memory;
}

// Функция для разбора JSON и вывода текущей погоды
void parse_and_print_weather(const char *json_str, const char *city)
{
    struct json_object *parsed_json;
    struct json_object *current_condition_array;
    struct json_object *current_condition;
    struct json_object *nearest_area_array;
    struct json_object *nearest_area;
    struct json_object *areaName_array;
    struct json_object *areaName;
    struct json_object *areaName_value;
    struct json_object *region_array;
    struct json_object *region;
    struct json_object *region_value;
    struct json_object *temp_C;
    struct json_object *weatherDesc_array;
    struct json_object *weatherDesc;
    struct json_object *weatherDesc_value;
    struct json_object *winddir16Point;
    struct json_object *windspeedKmph;

    // Парсим строку JSON

    parsed_json = json_tokener_parse(json_str);
    if (!parsed_json)
    {
        fprintf(stderr, "Ошибка: не удалось разобрать JSON.\n");
        return;
    }
    // разбираемся с локацией
    // Получаем массив nearest_area
    if (!json_object_object_get_ex(parsed_json, "nearest_area", &nearest_area_array))
    {
        fprintf(stderr, "Ошибка: в JSON нет поля nearest_area.\n");
        json_object_put(parsed_json);
        return;
    }

    nearest_area = json_object_array_get_idx(nearest_area_array, 0);

    printf("На ваш запрос погоды в %s\n", city);
puts("Получены данные :" );
    // Извлекаем регион (region[0].value)
    if (json_object_object_get_ex(nearest_area, "region", &region_array))
    {
        if (json_object_array_length(region_array) > 0)
        {
            region = json_object_array_get_idx(region_array, 0);
            if (json_object_object_get_ex(region, "value", &region_value))
            {
                 printf("Регион: %s    ", json_object_get_string(region_value));
            }
            else
            {
                printf("Не удалось получить регион.\n");
            }
        }
    }
    // Извлекаем локацию (areaName[0].value)
    if (json_object_object_get_ex(nearest_area, "areaName", &areaName_array))
    {
        if (json_object_array_length(areaName_array) > 0)
        {
            areaName = json_object_array_get_idx(areaName_array, 0);
            if (json_object_object_get_ex(areaName, "value", &areaName_value))
            {
                printf("локация: %s.\n", json_object_get_string(areaName_value));
            }
            else
            {
                printf("Не удалось получить локацию.\n");
            }
        }
    }

    // получаем погоду
    // Получаем массив current_condition
    if (!json_object_object_get_ex(parsed_json, "current_condition", &current_condition_array))
    {
        fprintf(stderr, "Ошибка: в JSON нет поля current_condition.\n");
        json_object_put(parsed_json);
        return;
    }

    // current_condition — это массив, берем первый элемент
    if (json_object_array_length(current_condition_array) < 1)
    {
        fprintf(stderr, "Ошибка: массив current_condition пуст.\n");
        json_object_put(parsed_json);
        return;
    }
    current_condition = json_object_array_get_idx(current_condition_array, 0);

    // Извлекаем описание погоды (weatherDesc[0].value)
    if (json_object_object_get_ex(current_condition, "weatherDesc", &weatherDesc_array))
    {
        if (json_object_array_length(weatherDesc_array) > 0)
        {
            weatherDesc = json_object_array_get_idx(weatherDesc_array, 0);
            if (json_object_object_get_ex(weatherDesc, "value", &weatherDesc_value))
            {
                printf("Погода: %s\n", json_object_get_string(weatherDesc_value));
            }
            else
            {
                printf("Не удалось получить погоду.\n");
            }
        }
    }

    // Извлекаем температуру (temp_C)
    if (json_object_object_get_ex(current_condition, "temp_C", &temp_C))
    {

        printf("Температура: %s°C\n", json_object_get_string(temp_C));
    }
    else
    {
        printf("Не удалось получить температуру.\n");
    }

    // Извлекаем направление ветра (winddir16Point)
    if (json_object_object_get_ex(current_condition, "winddir16Point", &winddir16Point))
    {
        printf("Направление ветра: %s\n", json_object_get_string(winddir16Point));
    }
    else
    {
        printf("Не удалось получить направление ветра.\n");
    }
    // Извлекаем скорость ветра (windspeedKmph)
    if (json_object_object_get_ex(current_condition, "windspeedKmph", &windspeedKmph))
    {
        printf("Скорость ветра: %sкм/ч\n", json_object_get_string(windspeedKmph));
    }
    else
    {
        printf("Не удалось получить скорость ветра.\n");
    }

    // Освобождаем память
    json_object_put(parsed_json);
}

/* ------------------ */
/* main               */
/* -------------------*/

int main( int argc , char *argv[]) {
if (argc != 2) {
fprintf(stderr, "Использование: %s <город>\n", argv[0]);
return 1;
}


    const char *city = argv[1];
    char *json_response = receiv_weather_curl(city);

    if (json_response)
    {
        parse_and_print_weather(json_response, city);
        free(json_response);
    }
    else
    {
        fprintf(stderr, "Не удалось получить данные о погоде.\n");
        return 1;
    }

    return 0;
}

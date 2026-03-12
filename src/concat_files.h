#ifndef CONCAT_FILES_H
#define CONCAT_FILES_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/**
 * Объединяет несколько CSV-файлов в один, добавляя столбец с именем источника.
 * - Берёт заголовок только из первого файла.
 * - Пропускает первую строку (заголовок) во всех последующих файлах.
 * - Проверяет, что количество столбцов одинаковое во всех файлах.
 * - Если --output не указан, генерирует имя merged_ГГГГММДД_ЧЧММСС.csv
 * - В новый столбец пишет базовое имя файла без пути и без .csv
 *
 * @param files             массив путей к входным файлам
 * @param file_count        количество файлов
 * @param source_col_name   имя нового столбца (не NULL)
 * @param user_output       желаемое имя выходного файла (может быть NULL)
 * @param result_filename   [out] сюда будет записан указатель на имя использованного файла
 *                          (функция сама выделит память под него — нужно free)
 * @return 0 — успех, иначе код ошибки (1 — общая ошибка, 2 — разное кол-во столбцов и т.д.)
 */
int concat_and_save_files(
    char **files,
    int file_count,
    const char *source_col_name,
    const char *user_output,
    char **result_filename
);

#endif /* CONCAT_FILES_H */
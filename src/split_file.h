#ifndef SPLIT_FILE_H
#define SPLIT_FILE_H

/**
 * Разбивает один CSV-файл на несколько по уникальным значениям столбца.
 *
 * Использование:
 *   csvview --split --by=region sales.csv
 *   csvview --split --by=2 sales.csv              (2 = номер столбца, 1-based)
 *   csvview --split --by=region --output-dir=./parts sales.csv
 *
 * Каждый выходной файл: <basename>_<value>.csv
 * Заголовок копируется в каждый выходной файл.
 *
 * @param input_path   путь к входному CSV файлу
 * @param by_col       имя столбца или его номер (1-based) для группировки
 * @param output_dir   каталог для выходных файлов (NULL = рядом с входным)
 * @param drop_col     1 — удалить столбец-разделитель из выходных файлов, 0 — оставить
 * @return 0 — успех, иначе код ошибки
 */
int split_file(const char *input_path, const char *by_col, const char *output_dir, int drop_col);

#endif /* SPLIT_FILE_H */

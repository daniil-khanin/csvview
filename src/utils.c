#include "utils.h"
#include <time.h>

// Parse a double, accepting both dot and comma as decimal separator.
double parse_double(const char *s, char **endptr)
{
    if (!s) { if (endptr) *endptr = (char *)s; return 0.0; }

    // Copy and replace comma→dot
    char buf[64];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    for (char *p = buf; *p; p++) {
        if (*p == ',') *p = '.';
    }

    char *ep;
    double result = strtod(buf, &ep);
    // Map endptr back into the original string
    if (endptr) *endptr = (char *)s + (ep - buf);
    return result;
}

/**
 * @brief Преобразует номер столбца (0-based) в буквенное обозначение Excel-стиля
 *
 * Преобразует целочисленный индекс столбца (начиная с 0) в строку вида:
 *   0  → "A"
 *   1  → "B"
 *   25 → "Z"
 *   26 → "AA"
 *   702 → "AAA"  и т.д.
 *
 * @param col       Номер столбца (0 = A, 1 = B, ..., 25 = Z, 26 = AA, ...)
 *                  Если col < 0 — функция просто очищает буфер и возвращается
 * @param buf       Указатель на буфер, куда будет записан результат (строка с нулевым терминатором)
 *                  Буфер должен быть достаточно большим (рекомендуется ≥ 16 байт)
 *
 * @note
 *   - Функция записывает результат в виде строки в верхнем регистре
 *   - Максимально поддерживаемый номер столбца зависит от размера буфера
 *     (при buf[16] — до "XFD" ≈ 16383 столбец, как в Excel 2007+)
 *   - Если передан отрицательный номер — буфер очищается (пустая строка)
 *
 * @example
 *   char buf[16];
 *   col_letter(0, buf);   // buf → "A"
 *   col_letter(25, buf);  // buf → "Z"
 *   col_letter(26, buf);  // buf → "AA"
 *   col_letter(701, buf); // buf → "ZZ"
 *   col_letter(702, buf); // buf → "AAA"
 *
 * @warning Буфер должен быть инициализирован и иметь размер не менее 8–16 байт
 *          в зависимости от того, насколько большие номера столбцов вы ожидаете.
 */
void col_letter(int col, char *buf)
{
    buf[0] = '\0';
    if (col < 0) return;

    char temp[16];
    int i = 0;

    // Преобразуем число в систему счисления base-26, но с особенностью Excel:
    // нет "нулевого" символа, поэтому делаем col / 26 - 1
    do {
        temp[i++] = 'A' + (col % 26);
        col = col / 26 - 1;
    } while (col >= 0);

    // Разворачиваем результат (т.к. мы собирали символы с младшего разряда)
    int j = 0;
    while (i > 0) {
        buf[j++] = temp[--i];
    }
    buf[j] = '\0';
}

/**
 * @brief Преобразует буквенное обозначение столбца Excel-стиля в его числовой индекс (0-based)
 *
 * Преобразует строку вида "A", "B", "Z", "AA", "AB", "ZZ", "AAA" и т.д. в номер столбца,
 * где:
 *   "A"   → 0
 *   "B"   → 1
 *   "Z"   → 25
 *   "AA"  → 26
 *   "AB"  → 27
 *   "ZZ"  → 701
 *   "AAA" → 702
 *
 * @param label     Указатель на строку с обозначением столбца (например "AB", "ZZ")
 *                  Допускаются только заглавные буквы A–Z
 *                  Пустая строка или NULL → возврат -1
 *
 * @return
 *   ≥ 0    — успешное преобразование, номер столбца (0-based)
 *   -1     — ошибка: неверный формат, пустая строка, содержит недопустимые символы
 *
 * @note
 *   - Функция строго проверяет, что строка состоит ТОЛЬКО из заглавных букв A–Z
 *   - Алгоритм соответствует системе нумерации столбцов Microsoft Excel / Google Sheets
 *   - Максимально допустимое значение зависит от длины строки и размера int
 *     (при 32-битном int обычно до ~7 букв ≈ столбец 8 млрд)
 *
 * @example
 *   col_to_num("A")    // → 0
 *   col_to_num("Z")    // → 25
 *   col_to_num("AA")   // → 26
 *   col_to_num("AZ")   // → 51
 *   col_to_num("ZZ")   // → 701
 *   col_to_num("AAA")  // → 702
 *   col_to_num("abc")  // → -1 (маленькие буквы)
 *   col_to_num("A1")   // → -1 (цифры недопустимы)
 *   col_to_num("")     // → -1
 *   col_to_num(NULL)   // → -1
 *
 * @see col_letter() — обратная функция (число → буквы)
 */
int col_to_num(const char *label)
{
    // Проверка на NULL или пустую строку
    if (!label || !*label) {
        return -1;
    }

    int num = 0;

    // Проходим по каждому символу строки
    while (*label) {
        // Проверяем, что символ — строго заглавная буква A–Z
        if (*label < 'A' || *label > 'Z') {
            return -1;
        }

        // Преобразуем в систему счисления base-26
        // ('A' = 1, 'B' = 2, ..., 'Z' = 26)
        num = num * 26 + (*label - 'A' + 1);
        label++;
    }

    // Excel-нумерация начинается с 1 внутри системы base-26,
    // поэтому отнимаем 1, чтобы получить 0-based индекс
    return num - 1;
}

/**
 * @brief Находит номер столбца (0-based) по его текстовому имени (заголовку)
 *
 * Производит поиск среди всех известных имён столбцов (из заголовка CSV-файла)
 * и возвращает индекс первого столбца, у которого имя совпадает с переданной строкой.
 * Сравнение выполняется с учётом регистра (case-sensitive).
 *
 * @param name      Указатель на строку с именем столбца (например "Дата", "Revenue", "User_ID")
 *                  Если name == NULL или строка пустая — функция вернёт -1
 *
 * @return
 *   ≥ 0    — индекс столбца (0-based), у которого совпадает имя
 *   -1     — столбец с таким именем не найден (или name == NULL / пустая строка)
 *
 * @note
 *   - Функция работает только если заголовки столбцов были успешно прочитаны
 *     (т.е. массив column_names[] заполнен, а col_count > 0)
 *   - Сравнение строгое: strcmp() → различает "revenue" и "Revenue"
 *   - Если в таблице несколько столбцов с одинаковым именем — возвращается первый найденный
 *   - Используется в фильтрах, командах :cf, :cal, :fq и других местах, где нужно
 *     обращаться к столбцу по человеческому имени, а не по букве (A/B/C)
 *
 * @example
 *   // Предположим, заголовки: "ID", "Name", "Price", "Date"
 *   col_name_to_num("Price")   // → 2
 *   col_name_to_num("DATE")    // → -1 (регистр не совпадает)
 *   col_name_to_num("id")      // → -1 (регистр не совпадает)
 *   col_name_to_num("Unknown") // → -1
 *   col_name_to_num("")        // → -1
 *   col_name_to_num(NULL)      // → -1
 *
 * @see
 *   - col_to_num()     — преобразование буквенного обозначения "AA" → номер
 *   - get_column_value() — получение значения ячейки по имени столбца
 */
int col_name_to_num(const char *name)
{
    // Быстрая защита от некорректного ввода
    if (!name || !*name) {
        return -1;
    }

    // Линейный поиск по массиву имён столбцов
    for (int c = 0; c < col_count; c++) {
        // Проверяем, что имя столбца существует и совпадает
        if (column_names[c] && strcmp(column_names[c], name) == 0) {
            return c;  // Нашли — возвращаем индекс (0-based)
        }
    }

    // Не нашли ни одного совпадения
    return -1;
}

/**
 * @brief Сохраняет текущее состояние таблицы (все строки) в указанный CSV-файл
 *
 * Создаёт временный файл, записывает в него все строки из индекса `rows`,
 * затем атомарно заменяет оригинальный файл на новый с помощью rename().
 * 
 * Если у строки есть кэш (`line_cache`), используется он.
 * Если кэша нет — строка читается из оригинального файла по смещению `offset`.
 *
 * @param filename      Полный путь к целевому CSV-файлу (куда сохраняем)
 * @param orig_f        Открытый FILE* исходного файла (нужен для чтения строк,
 *                      у которых ещё нет line_cache). Должен быть открыт в режиме "r".
 * @param rows          Массив индексов строк (RowIndex), содержащий либо кэш строки,
 *                      либо смещение в исходном файле
 * @param row_count     Количество строк в массиве rows, которые нужно сохранить
 *                      (обычно равно глобальной переменной row_count)
 *
 * @return
 *    0    — успех: файл успешно перезаписан
 *   -1    — ошибка:
 *           • не удалось создать/открыть временный файл
 *           • ошибка чтения из orig_f (fseek/fgets)
 *           • не удалось переименовать временный файл в целевой (rename)
 *
 * @note
 *   - Функция **атомарна** на уровне файловой системы благодаря rename()
 *     (либо старый файл остаётся, либо сразу появляется новый)
 *   - Все строки записываются с добавлением '\n' в конец
 *   - Если строка была пустой или не прочиталась → записывается пустая строка + '\n'
 *   - Оригинальный файл **не закрывается** внутри функции — ответственность вызывающего кода
 *   - Используется при:
 *     • удалении строк (:dr)
 *     • переименовании столбца (:cr)
 *     • изменении содержимого столбца (:cf)
 *     • добавлении столбца (:cal / :car)
 *
 * @example
 *    // Пример: сохранить все строки после удаления одной
 *    if (save_file("data.csv", original_file, rows, new_row_count) == 0) {
 *        printf("File saved successfully\n");
 *    } else {
 *        fprintf(stderr, "Save failed\n");
 *    }
 *
 * @warning
 *   - Если orig_f повреждён или смещения в rows некорректны — часть строк может быть потеряна
 *   - Не проверяется, существует ли директория и есть ли права на запись
 *   - При ошибке rename() временный файл (.tmp) остаётся на диске
 *     (рекомендуется в вызывающем коде удалять его при ошибке)
 *
 * @see
 *   - load_saved_filters(), save_filter() — похожая логика работы с конфигом
 *   - apply_filter(), build_sorted_index() — часто вызывают save_file после изменений
 */
int save_file(const char *filename, FILE *orig_f, RowIndex *rows, int row_count)
{
    char temp_name[1024];
    snprintf(temp_name, sizeof(temp_name), "%s.tmp", filename);

    FILE *out = fopen(temp_name, "w");
    if (!out) {
        return -1;
    }

    for (int r = 0; r < row_count; r++)
    {
        /* buf объявлен здесь — действует весь цикл, line не станет visage-указателем */
        char buf[MAX_LINE_LEN];
        const char *line = rows[r].line_cache ? rows[r].line_cache : "";

        // Если кэша нет — читаем строку из оригинального файла
        if (!rows[r].line_cache)
        {
            if (fseek(orig_f, rows[r].offset, SEEK_SET) == 0)
            {
                if (fgets(buf, sizeof(buf), orig_f))
                {
                    // Убираем \r\n (fprintf ниже сам добавит \n)
                    buf[strcspn(buf, "\r\n")] = '\0';
                    line = buf;
                }
            }
        }

        // Записываем строку + перевод строки
        fprintf(out, "%s\n", line);
    }

    fclose(out);

    // Атомарная замена оригинального файла
    if (rename(temp_name, filename) == 0) {
        return 0;
    }

    // Если rename не удался — временный файл остаётся
    return -1;
}

/**
 * @brief Регистронезависимый поиск подстроки (аналог strcasestr, но portable-версия)
 *
 * Ищет первое вхождение подстроки `needle` в строке `haystack`, игнорируя регистр символов.
 * Сравнение выполняется с помощью `strncasecmp` (POSIX), поэтому работает на большинстве
 * UNIX-подобных систем и совместима с большинством компиляторов.
 *
 * @param haystack  Указатель на строку, в которой выполняется поиск (может быть NULL)
 * @param needle    Указатель на искомую подстроку (что ищем)
 *                  Если needle == NULL или needle — пустая строка → возвращается haystack
 *
 * @return
 *   - Указатель на первое вхождение подстроки в haystack (регистр не учитывается)
 *   - NULL, если подстрока не найдена
 *   - Сам haystack, если needle — пустая строка или NULL
 *
 * @note
 *   - Функция **не** изменяет исходные строки
 *   - Возвращаемый указатель указывает внутрь haystack (не нужно free)
 *   - Сравнение идёт посимвольно с помощью strncasecmp → зависит от текущей локали
 *   - Эффективность: O(n·m) в худшем случае (где n = strlen(haystack), m = strlen(needle))
 *   - Это кастомная реализация, потому что strcasestr есть не везде (например, в glibc есть,
 *     но в некоторых BSD, Windows MSVC её нет по умолчанию)
 *
 * @example
 *   const char *text = "Hello World, hello again!";
 *   char *pos;
 *
 *   pos = strcasestr_custom(text, "hello");     // → указатель на "hello" (второй раз)
 *   pos = strcasestr_custom(text, "WORLD");     // → указатель на "World"
 *   pos = strcasestr_custom(text, "");          // → text (само начало строки)
 *   pos = strcasestr_custom(text, "python");    // → NULL
 *   pos = strcasestr_custom(NULL, "test");      // → NULL (хотя haystack NULL)
 *   pos = strcasestr_custom("AbC", NULL);       // → "AbC" (needle пустое)
 *
 * @see
 *   - strcasestr()   — POSIX-расширение (есть не везде)
 *   - strstr()       — регистрозависимый аналог
 *   - strncasecmp()  — используется внутри для сравнения
 */
char *strcasestr_custom(const char *haystack, const char *needle)
{
    // Специальный случай: пустая или NULL подстрока → возвращаем начало haystack
    if (!needle || !*needle) {
        return (char *)haystack;
    }

    size_t nlen = strlen(needle);
    const char *p = haystack;

    // Ищем посимвольно, сравнивая подстроку длиной nlen
    while (*p) {
        // Регистронезависимое сравнение первых nlen символов
        if (strncasecmp(p, needle, nlen) == 0) {
            return (char *)p;  // Нашли — возвращаем указатель на начало вхождения
        }
        p++;
    }

    // Не нашли ни одного вхождения
    return NULL;
}

/**
 * @brief Удаляет начальные и конечные пробельные символы из строки (in-place)
 *
 * Модифицирует переданную строку, убирая все пробельные символы (пробел, таб, перевод строки и т.д.)
 * слева и справа. Результат записывается в ту же память, возвращается указатель на начало
 * очищенной строки (может быть сдвинут вправо).
 *
 * @param str    Указатель на строку, которую нужно обрезать (null-terminated)
 *               Если str == NULL или строка пустая — возвращается без изменений
 *
 * @return
 *   - Указатель на начало очищенной строки (часто тот же, что str, но может быть сдвинут)
 *   - Сам str, если вход был NULL или пустая строка
 *
 * @note
 *   - Функция работает **in-place** — изменяет содержимое исходного буфера
 *   - Использует isspace() → удаляет все символы, считающиеся пробельными в текущей локали
 *     (пробел ' ', табуляция '\t', новая строка '\n', возврат каретки '\r', вертикальный таб и т.д.)
 *   - Если после обрезки строка становится пустой — возвращается указатель на '\0'
 *   - Безопасна для пустых строк и строк, состоящих только из пробелов
 *   - НЕ выделяет новую память — НЕ нужно free() возвращаемый указатель
 *
 * @example
 *   char s1[] = "   Hello World   ";
 *   trim(s1);               // s1 → "Hello World\0"   (возвращает &s1[3])
 *
 *   char s2[] = "   \t\n  ";
 *   trim(s2);               // s2 → "\0"   (пустая строка)
 *
 *   char s3[] = "NoSpacesHere";
 *   trim(s3);               // s3 остаётся "NoSpacesHere"
 *
 *   char *s4 = NULL;
 *   trim(s4);               // возвращает NULL
 *
 *   char s5[] = "";
 *   trim(s5);               // возвращает s5 (указатель на '\0')
 *
 * @warning
 *   - Строка должна быть в изменяемой памяти (не строковый литерал "const char*")
 *     Иначе → segmentation fault при попытке записи
 *     Пример плохого использования:
 *       char *bad = "   test   ";   // строковый литерал, обычно в read-only памяти
 *       trim(bad);                  // → UB (undefined behavior)
 *   - Если строка очень длинная — strlen() вызывается один раз, но всё равно O(n)
 *
 * @see
 *   - Многие реализации trim в других языках (Python .strip(), Java .trim())
 *   - ltrim(), rtrim() — если нужно отдельно убрать только слева или только справа
 */
char *trim(char *str)
{
    // Если NULL или уже пустая строка — возвращаем как есть
    if (!str || !*str) {
        return str;
    }

    // Убираем пробелы (и другие isspace-символы) слева
    while (isspace((unsigned char)*str)) {
        str++;
    }

    // Если после удаления слева строка закончилась — возвращаем указатель на '\0'
    if (*str == '\0') {
        return str;
    }

    // Ищем конец строки
    char *end = str + strlen(str) - 1;

    // Убираем пробелы справа, двигаясь влево
    while (end > str && isspace((unsigned char)*end)) {
        end--;
    }

    // Ставим нулевой терминатор сразу после последнего непробельного символа
    *(end + 1) = '\0';

    // Возвращаем указатель на начало очищенной строки
    return str;
}

/**
 * @brief Парсит строку фильтра в структурированное выражение (FilterExpr)
 *
 * Разбирает пользовательский запрос фильтра в формате:
 *   [ ! ] column1 >= 100 AND column2 = "Active" OR column3 != "Blocked"
 *
 * Поддерживает:
 *   - Отрицание всего выражения через ! в начале
 *   - Операторы: =, !=, >, >=, <, <=
 *   - Логические связки: AND, OR (регистронезависимо)
 *   - Значения в кавычках (удаляются автоматически)
 *   - Пробелы в любом разумном количестве
 *
 * @param query     Указатель на строку с запросом фильтра (например "Price > 500 AND Status = Active")
 *                  Может быть NULL или пустой — тогда возврат -1
 * @param expr      Указатель на структуру FilterExpr, в которую будет записан результат разбора
 *                  Поля expr->conditions и expr->logic_ops выделяются динамически внутри функции
 *                  (их нужно освободить через free_filter_expr() после использования!)
 *
 * @return
 *    0    — успех: выражение успешно распарсено, expr заполнен
 *   -1    — ошибка:
 *           • query == NULL или пустая строка
 *           • не удалось выделить память (strdup/malloc)
 *           • не найдено ни одного корректного условия
 *           • синтаксическая ошибка (нет оператора, некорректное имя столбца и т.д.)
 *
 * @note
 *   - Выделяет память под имена столбцов и значения (strdup) — нужно освободить через free_filter_expr(expr)
 *   - Поддерживает до 64 условий (ограничение temp_conds[64])
 *   - Значения автоматически определяются как числовые (strtod), если возможно
 *   - Кавычки (" или ') вокруг значений снимаются автоматически
 *   - Логические операторы AND/OR должны быть отделены пробелами (или концом строки)
 *   - Отрицание (!) применяется ко всему выражению, а не к отдельным условиям
 *
 * @example
 *   FilterExpr expr = {0};
 *   int res = parse_filter_expression("Age >= 18 AND City = \"Moscow\" OR Status != Blocked", &expr);
 *   // res == 0 → expr содержит 3 условия, 2 оператора (AND, OR)
 *
 *   parse_filter_expression("! Price < 100", &expr);
 *   // → expr->negated == 1, одно условие Price < 100
 *
 *   parse_filter_expression("Status = Active", &expr);
 *   // → одно условие, negated == 0
 *
 *   parse_filter_expression("", &expr);           // → -1
 *   parse_filter_expression("Age >", &expr);      // → -1 (нет значения)
 *   parse_filter_expression("x = y AND", &expr);  // → -1 (неполное выражение)
 *
 * @warning
 *   - Если функция вернула 0 — обязательно вызовите free_filter_expr(expr) после использования,
 *     иначе будет утечка памяти!
 *   - Не поддерживает вложенные скобки, приоритеты или сложные выражения — только линейная цепочка AND/OR
 *   - Имена столбцов не могут содержать символы ><=! (и пробелы — обрезаются trim)
 *
 * @see
 *   - free_filter_expr()    — обязательная очистка после использования
 *   - row_matches_filter()  — применение распарсенного выражения к строке
 *   - apply_filter()        — основной потребитель результата этой функции
 */
int parse_filter_expression(const char *query, FilterExpr *expr)
{
    // Защита от пустого или некорректного ввода
    if (!query || !*query) {
        return -1;
    }

    // Делаем рабочую копию строки, чтобы можно было модифицировать
    char *input = strdup(query);
    if (!input) {
        return -1;
    }

    // Убираем лишние пробелы в начале и конце
    char *p = trim(input);

    // Поддержка скобок: заменяем ( и ) пробелами перед парсингом.
    // При левостороннем вычислении (A OR B) AND C корректно раскрывается
    // в A OR B AND C → результат тот же.
    for (char *pp = p; *pp; pp++) {
        if (*pp == '(' || *pp == ')') *pp = ' ';
    }
    p = trim(p);

    // Проверяем отрицание всего выражения
    expr->negated = 0;
    if (*p == '!') {
        expr->negated = 1;
        p = trim(p + 1);
    }

    Condition temp_conds[64];           // временный массив условий (макс. 64)
    LogicOperator temp_ops[63];         // операторы между ними (на один меньше)
    int cond_count = 0;

    // Основной цикл парсинга
    while (*p && cond_count < 64)
    {
        // Пропускаем лишние пробелы
        while (isspace(*p)) p++;
        if (!*p) break;

        // Проверяем, не логический ли это оператор (AND / OR)
        if (strncasecmp(p, "AND", 3) == 0 && (isspace(p[3]) || !p[3])) {
            if (cond_count > 0) temp_ops[cond_count-1] = LOGIC_AND;
            p += 3;
            continue;
        }
        if (strncasecmp(p, "OR", 2) == 0 && (isspace(p[2]) || !p[2])) {
            if (cond_count > 0) temp_ops[cond_count-1] = LOGIC_OR;
            p += 2;
            continue;
        }

        // Парсим условие: имя_столбца [пробелы] оператор [пробелы] значение
        char col_name[128] = {0};
        char *col_start = p;
        while (*p && !strchr("><=! \t", *p)) p++;
        int col_len = p - col_start;
        if (col_len >= (int)sizeof(col_name)) col_len = (int)sizeof(col_name)-1;
        strncpy(col_name, col_start, col_len);
        trim(col_name);

        if (!*col_name) break;  // пустое имя столбца — конец разбора

        // Пропускаем пробелы перед оператором
        while (isspace(*p)) p++;

        // Определяем оператор сравнения (двухсимвольные проверяем первыми!)
        CompareOp op = -1;
        int op_len = 0;
        if      (p[0] == '>' && p[1] == '=') { op = OP_GE; op_len = 2; }
        else if (p[0] == '<' && p[1] == '=') { op = OP_LE; op_len = 2; }
        else if (p[0] == '!' && p[1] == '=') { op = OP_NE; op_len = 2; }
        else if (p[0] == '=' && p[1] == '=') { op = OP_EQ; op_len = 2; } // редкий случай
        else if (p[0] == '>')                { op = OP_GT; op_len = 1; }
        else if (p[0] == '<')                { op = OP_LT; op_len = 1; }
        else if (p[0] == '=')                { op = OP_EQ; op_len = 1; }

        if ((int)op == -1) {
            break;  // нет оператора — синтаксическая ошибка
        }

        p += op_len;

        // Пропускаем пробелы перед значением
        while (isspace(*p)) p++;

        // Считываем значение до следующего AND/OR или конца строки
        char val_buf[256] = {0};
        char *val_start = p;
        while (*p) {
            if (strncasecmp(p, " AND ", 5) == 0 || strncasecmp(p, " OR ", 4) == 0) break;
            p++;
        }
        int val_len = p - val_start;
        if (val_len >= (int)sizeof(val_buf)) val_len = (int)sizeof(val_buf)-1;
        strncpy(val_buf, val_start, val_len);
        trim(val_buf);

        // Убираем кавычки, если они есть
        if ((val_buf[0] == '"' || val_buf[0] == '\'') &&
            val_buf[strlen(val_buf)-1] == val_buf[0])
        {
            memmove(val_buf, val_buf + 1, strlen(val_buf) - 2);
            val_buf[strlen(val_buf) - 2] = '\0';
        }

        // Заполняем временную структуру условия
        temp_conds[cond_count].column       = strdup(col_name);
        temp_conds[cond_count].op           = op;
        temp_conds[cond_count].value        = strdup(val_buf);
        temp_conds[cond_count].value_is_num = 0;
        temp_conds[cond_count].value_num    = 0.0;

        // Пытаемся распознать число
        char *endptr;
        double num = parse_double(val_buf, &endptr);
        if (endptr != val_buf && *endptr == '\0') {
            temp_conds[cond_count].value_is_num = 1;
            temp_conds[cond_count].value_num    = num;
        }

        cond_count++;

        // Пропускаем пробелы после значения (перед следующим AND/OR)
        while (isspace(*p)) p++;
    }

    // Если не удалось распарсить ни одного условия — ошибка
    if (cond_count == 0) {
        free(input);
        return -1;
    }

    // Копируем результат в выходную структуру
    expr->cond_count = cond_count;
    expr->conditions = malloc(cond_count * sizeof(Condition));
    if (!expr->conditions) {
        free(input);
        return -1;
    }
    memcpy(expr->conditions, temp_conds, cond_count * sizeof(Condition));

    // Логические операторы (если условий больше одного)
    expr->logic_ops = NULL;
    if (cond_count > 1) {
        expr->logic_ops = malloc((cond_count - 1) * sizeof(LogicOperator));
        if (!expr->logic_ops) {
            free(expr->conditions);
            free(input);
            return -1;
        }
        memcpy(expr->logic_ops, temp_ops, (cond_count - 1) * sizeof(LogicOperator));
    }

    free(input);
    return 0;
}

/**
 * @brief Извлекает значение ячейки из строки CSV по имени столбца
 *
 * Парсит одну строку CSV и возвращает значение поля, соответствующего указанному имени столбца.
 * Поддерживает корректную работу с экранированными кавычками (стандарт RFC 4180).
 * Возвращает новую выделенную строку (malloc), которую **обязательно** нужно освободить через free().
 *
 * @param line          Указатель на строку CSV (одна запись, null-terminated)
 *                      Может содержать экранированные кавычки и запятые внутри полей
 * @param col_name      Имя столбца, значение которого нужно найти
 *                      Сравнение регистронезависимое (strcasecmp)
 *                      Если col_name пустое или NULL → возвращается пустая строка
 * @param use_headers   Флаг использования заголовков:
 *                        1 — имена столбцов берутся из массива column_names[]
 *                        0 — имена генерируются как A, B, C, ..., AA, AB и т.д.
 *
 * @return
 *   - Новая строка (malloc), содержащая значение ячейки (без кавычек)
 *   - Пустая строка "" (тоже через strdup), если:
 *     • столбец не найден
 *     • строка пустая / некорректная
 *     • ошибка выделения памяти
 *     • переданы NULL или пустые аргументы
 *
 * @note
 *   - Функция **всегда** возвращает новую выделенную память → нужно free() результат!
 *   - Парсер учитывает экранирование кавычек внутри полей ("He said ""hello""")
 *   - Сравнение имени столбца регистронезависимое (strcasecmp) — удобно для пользователей
 *   - Если use_headers=1, но column_names[col_idx] == NULL — переключается на буквенное имя
 *   - Максимальное количество столбцов ограничено MAX_COLS
 *
 * @example
 *   // Предположим заголовки: "ID", "Name", "Price"
 *   char *line = "101,\"Anna Smith\",29.99";
 *
 *   char *val1 = get_column_value(line, "Price", 1);     // → "29.99" (нужно free)
 *   char *val2 = get_column_value(line, "name", 1);      // → "Anna Smith" (регистр не важен)
 *   char *val3 = get_column_value(line, "C", 0);         // → "29.99" (столбец C = третий)
 *   char *val4 = get_column_value(line, "Unknown", 1);   // → ""
 *
 *   free(val1); free(val2); free(val3); free(val4);      // обязательно!
 *
 * @warning
 *   - Возвращаемый указатель **всегда** нужно освободить через free(),
 *     даже если вернулась пустая строка ("")
 *   - Не модифицирует исходную строку line
 *   - При очень длинных строках (> MAX_LINE_LEN в других частях кода) может работать некорректно,
 *     но внутри использует только strdup(line), так что ограничена доступной памятью
 *
 * @see
 *   - col_name_to_num()     — получение индекса столбца по имени
 *   - evaluate_condition()  — использует эту функцию при проверке фильтров
 *   - row_matches_filter()  — основной потребитель
 */
char *get_column_value(const char *line, const char *col_name, int use_headers)
{
    if (!line || !*line || !col_name || !*col_name)
        return strdup("");

    int count;
    char **fields = parse_csv_line(line, &count);
    if (!fields)
        return strdup("");

    char *result = NULL;
    char letter[16];

    for (int i = 0; i < count; i++)
    {
        const char *current_name;
        if (use_headers && i < MAX_COLS && column_names[i])
        {
            current_name = column_names[i];
        }
        else
        {
            col_letter(i, letter);
            current_name = letter;
        }

        if (strcasecmp(current_name, col_name) == 0)
        {
            result = strdup(fields[i]);
            break;
        }
    }

    free_csv_fields(fields, count);
    return result ? result : strdup("");
}

/**
 * @brief Проверяет, удовлетворяет ли значение ячейки заданному условию сравнения
 *
 * Сравнивает значение ячейки (`cell`) с условием из структуры `Condition`.
 * Поддерживает два режима сравнения:
 *   - числовое (если значение в условии удалось распарсить как double)
 *   - строковое (лексикографическое, с помощью strcmp)
 *
 * @param cell      Указатель на строку — значение ячейки из CSV (может быть NULL)
 *                  Если cell == NULL, считается пустой строкой ""
 * @param cond      Указатель на структуру условия (Condition), содержащую:
 *                    • op          — оператор сравнения (OP_EQ, OP_NE, OP_GT и т.д.)
 *                    • value       — строка-значение для сравнения
 *                    • value_is_num — флаг, что значение числовое
 *                    • value_num   — числовое представление (если value_is_num == 1)
 *
 * @return
 *    1    — условие выполнено (ячейка соответствует условию)
 *    0    — условие НЕ выполнено ИЛИ произошла ошибка:
 *           • неизвестный оператор
 *           • ячейка не удалось распарсить как число (при числовом сравнении)
 *           • передан NULL в cond (но это не проверяется явно)
 *
 * @note
 *   - При **числовом** сравнении:
 *     • Если ячейка не является валидным числом (например "abc", "12.3.4", "")
 *       → условие считается ложным (возврат 0)
 *     • Используется strtod → учитывает локаль (десятичный разделитель . или ,)
 *   - При **строковом** сравнении:
 *     • Поддерживаются все операторы, но >, <, >=, <= имеют смысл только для
 *       лексикографически упорядоченных данных (даты в ISO, коды и т.п.)
 *     • Сравнение регистрозависимое (strcmp)
 *   - Функция **не** выделяет память и **не** модифицирует входные данные
 *   - Очень важная внутренняя функция — используется в row_matches_filter()
 *
 * @example
 *   Condition cond = { .op = OP_GT, .value = "100", .value_is_num = 1, .value_num = 100.0 };
 *
 *   evaluate_condition("150", &cond)    → 1   (150 > 100)
 *   evaluate_condition("99", &cond)     → 0
 *   evaluate_condition("abc", &cond)    → 0   (не число)
 *   evaluate_condition("", &cond)       → 0
 *
 *   Condition cond2 = { .op = OP_EQ, .value = "Active", .value_is_num = 0 };
 *   evaluate_condition("active", &cond2) → 0   (регистр важен)
 *   evaluate_condition("Active", &cond2) → 1
 *
 * @warning
 *   - При строковом сравнении с операторами >/< результат может быть неожиданным
 *     для пользователя (например "10" < "2" → true, потому что '1' < '2')
 *   - Нет поддержки частичного соответствия, LIKE, регулярных выражений и т.п.
 *     (это только точное сравнение)
 *
 * @see
 *   - row_matches_filter()   — основное место использования
 *   - parse_filter_expression() — откуда берётся структура Condition
 *   - apply_filter()         — весь процесс фильтрации
 */
int evaluate_condition(const char *cell, const Condition *cond)
{
    // Защита: если ячейка NULL — считаем пустой строкой
    if (!cell) {
        cell = "";
    }

    int col_num = col_name_to_num(cond->column);
    if (col_num < 0) {
        // столбец не найден → считаем, что условие не выполнено
        return 0;
    }

    if (cond->value_is_num)
    {
        // Числовое сравнение
        char *endptr;
        double cell_num = parse_double(cell, &endptr);

        // Если не удалось полностью распарсить строку как число
        // (остались символы после числа или строка вообще не начиналась с числа)
        if (*endptr != '\0' || endptr == cell) {
            return 0;
        }

        switch (cond->op)
        {
            case OP_EQ: return cell_num == cond->value_num;
            case OP_NE: return cell_num != cond->value_num;
            case OP_GT: return cell_num >  cond->value_num;
            case OP_GE: return cell_num >= cond->value_num;
            case OP_LT: return cell_num <  cond->value_num;
            case OP_LE: return cell_num <= cond->value_num;
            default:    return 0;  // неизвестный оператор
        }
    }
    else if (col_types[col_num] == COL_DATE)
    {
        // Убираем лишние пробелы, если вдруг парсер добавил
        char val_trimmed[256];
        strncpy(val_trimmed, cond->value, sizeof(val_trimmed)-1);
        val_trimmed[sizeof(val_trimmed)-1] = '\0';
        trim(val_trimmed);   // твоя функция trim

        size_t val_len  = strlen(val_trimmed);
        size_t cell_len = strlen(cell);

        /* ── Quarter format: "2025-Q1" .. "2025-Q4" ── */
        if (val_len == 7 && val_trimmed[5] == 'Q' &&
            val_trimmed[6] >= '1' && val_trimmed[6] <= '4' &&
            cell_len >= 10 && cell[4] == '-')
        {
            int q = val_trimmed[6] - '0';
            int q_start_m = (q - 1) * 3 + 1;   /* Q1→1, Q2→4, Q3→7, Q4→10 */
            int q_next_m  = q * 3 + 1;          /* first month of next quarter */
            int val_year  = 0;
            for (int i = 0; i < 4; i++) val_year = val_year * 10 + (val_trimmed[i] - '0');
            int next_year = val_year + (q_next_m > 12 ? 1 : 0);
            if (q_next_m > 12) q_next_m = 1;

            /* "YYYY-MM" boundary strings for lexicographic compare */
            char q_start[8], q_next[8];
            snprintf(q_start, sizeof(q_start), "%04d-%02d", val_year, q_start_m);
            snprintf(q_next,  sizeof(q_next),  "%04d-%02d", next_year, q_next_m);

            if (cond->op == OP_EQ)
            {
                /* cell must be >= start of quarter AND < start of next quarter */
                return strncmp(cell, q_start, 7) >= 0 && strncmp(cell, q_next, 7) < 0;
            }
            if (cond->op == OP_NE)
            {
                return !(strncmp(cell, q_start, 7) >= 0 && strncmp(cell, q_next, 7) < 0);
            }
            /* >= Q: date must be >= first day of quarter start month */
            if (cond->op == OP_GE) return strncmp(cell, q_start, 7) >= 0;
            /* >  Q: date must be >= first month of next quarter */
            if (cond->op == OP_GT) return strncmp(cell, q_next,  7) >= 0;
            /* <= Q: date must be < first month of next quarter */
            if (cond->op == OP_LE) return strncmp(cell, q_next,  7) <  0;
            /* <  Q: date must be < first month of this quarter */
            if (cond->op == OP_LT) return strncmp(cell, q_start, 7) <  0;
            return 0;
        }

        if (cond->op == OP_EQ)
        {
            // Самый частый случай: ищем месяц "2026-01"
            if (val_len == 7 && cell_len >= 10 &&
                strncmp(cell, val_trimmed, 7) == 0 &&
                cell[7] == '-')
            {
                return 1;   // да, это январь 2026 (или любой день этого месяца)
            }

            // Если ввели полную дату — обычное равенство
            return strcmp(cell, val_trimmed) == 0;
        }

        // Все остальные операторы (>= > <= < !=) — лексикографическое сравнение строк
        int cmp = strcmp(cell, val_trimmed);

        switch (cond->op)
        {
            case OP_NE: return cmp != 0;
            case OP_GT: return cmp >  0;
            case OP_GE: return cmp >= 0;
            case OP_LT: return cmp <  0;
            case OP_LE: return cmp <= 0;
            default:    return 0;
        }
    }    
    else
    {
        // Строковое сравнение
        switch (cond->op)
        {
            case OP_EQ: return strcmp(cell, cond->value) == 0;
            case OP_NE: return strcmp(cell, cond->value) != 0;

            // Лексикографическое сравнение (может быть полезно для дат ISO, кодов и т.п.)
            case OP_GT: return strcmp(cell, cond->value) > 0;
            case OP_GE: return strcmp(cell, cond->value) >= 0;
            case OP_LT: return strcmp(cell, cond->value) < 0;
            case OP_LE: return strcmp(cell, cond->value) <= 0;

            default:    return 0;  // неизвестный оператор
        }
    }

    // Сюда попадаем только при неизвестном операторе (на всякий случай)
    return 0;
}

/**
 * @brief Проверяет, соответствует ли одна строка CSV заданному фильтру
 *
 * Применяет полностью распарсенное выражение фильтра (FilterExpr) к одной строке CSV.
 * Возвращает 1, если строка проходит все условия фильтра (с учётом AND/OR и отрицания !).
 *
 * @param line      Указатель на строку CSV (одна запись, null-terminated)
 *                  Должна быть валидной CSV-строкой (с запятыми, возможными кавычками)
 * @param expr      Указатель на распарсенное выражение фильтра (FilterExpr)
 *                  Если expr == NULL или в нём 0 условий → возвращается 1 (все строки проходят)
 *
 * @return
 *    1    — строка соответствует фильтру (все условия выполнены)
 *    0    — строка НЕ соответствует фильтру (хотя бы одно условие ложно)
 *
 * @note
 *   - Если фильтр пустой (cond_count == 0) или expr == NULL → возвращает 1
 *     (это важно для логики "без фильтра — показываем всё")
 *   - Условия применяются слева направо с учётом приоритета AND/OR
 *     (без скобок — просто последовательное вычисление)
 *   - При логическом AND, если промежуточный результат уже false — дальнейшие условия
 *     НЕ проверяются (ранний выход — оптимизация)
 *   - Отрицание (!) применяется ко **всему** выражению в целом
 *   - Для каждого условия вызывается get_column_value() → выделяет память,
 *     поэтому каждый вызов сопровождается free(val)
 *
 * @example
 *   // Предположим expr: "Age >= 18 AND City = \"Moscow\""
 *   row_matches_filter("101,Anna,25,Moscow", &expr)   → 1
 *   row_matches_filter("102,Bob,17,Moscow", &expr)    → 0  (возраст < 18)
 *   row_matches_filter("103,John,30,Kyiv", &expr)     → 0  (город не Moscow)
 *
 *   // expr с отрицанием: "! Status = Blocked"
 *   row_matches_filter("...,Active", &expr)   → 1
 *   row_matches_filter("...,Blocked", &expr)  → 0
 *
 *   // Пустой фильтр
 *   FilterExpr empty = {0};
 *   row_matches_filter(any_line, &empty) → 1 (всегда проходит)
 *
 * @warning
 *   - Функция **выделяет и освобождает** память для каждого значения ячейки
 *     (get_column_value → strdup внутри, free здесь)
 *   - Если строка CSV некорректна (незакрытые кавычки и т.п.) — поведение
 *     get_column_value может быть непредсказуемым, но обычно вернёт "" или часть строки
 *   - Нет защиты от очень большого количества условий (хотя парсер ограничивает 64)
 *
 * @see
 *   - parse_filter_expression() — создание структуры FilterExpr из строки
 *   - evaluate_condition()      — проверка одного условия
 *   - get_column_value()        — получение значения ячейки по имени
 *   - apply_filter()            — применение ко всем строкам файла
 */
int row_matches_filter(const char *line, const FilterExpr *expr)
{
    // Пустой фильтр или NULL-указатель → все строки проходят
    if (!expr || expr->cond_count == 0) {
        return 1;
    }

    // Обрабатываем первое условие
    char *val = get_column_value(line, expr->conditions[0].column, use_headers);
    int result = evaluate_condition(val, &expr->conditions[0]);
    free(val);

    // Проходим по остальным условиям (если они есть)
    for (int i = 1; i < expr->cond_count; i++)
    {
        val = get_column_value(line, expr->conditions[i].column, use_headers);
        int next = evaluate_condition(val, &expr->conditions[i]);
        free(val);

        // Применяем логический оператор между предыдущим результатом и текущим
        if (expr->logic_ops[i - 1] == LOGIC_AND)
        {
            result = result && next;
        }
        else  // LOGIC_OR
        {
            result = result || next;
        }

        // Оптимизация: при AND и уже ложном результате — дальше проверять бессмысленно
        if (!result && expr->logic_ops[i - 1] == LOGIC_AND) {
            break;
        }
    }

    // Учитываем отрицание всего выражения (! в начале)
    return expr->negated ? !result : result;
}

/**
 * @brief Освобождает всю динамически выделенную память внутри структуры FilterExpr
 *
 * Полностью очищает содержимое структуры FilterExpr, освобождая:
 *   - каждое имя столбца (column) и значение (value) в массиве conditions
 *   - сам массив условий (conditions)
 *   - массив логических операторов (logic_ops), если он был выделен
 *
 * После вызова структура становится "пустой" и безопасной для повторного использования
 * или уничтожения.
 *
 * @param expr      Указатель на структуру FilterExpr, память которой нужно освободить
 *                  Если expr == NULL — функция просто возвращается (безопасно)
 *
 * @return          Нет возвращаемого значения (void)
 *
 * @note
 *   - Функция **обязательна** к вызову после каждого успешного parse_filter_expression(),
 *     иначе будет утечка памяти!
 *   - Безопасно вызывать несколько раз на одной и той же структуре
 *     (повторные free(NULL) допустимы в C)
 *   - После вызова все указатели внутри expr становятся NULL, cond_count = 0
 *     (благодаря memset)
 *   - Не трогает саму структуру expr (не free(expr)), только её внутренние поля
 *   - Используется в apply_filter() после обработки фильтра,
 *     а также везде, где временно создаётся FilterExpr
 *
 * @example
 *   FilterExpr expr = {0};
 *   if (parse_filter_expression("Age > 18 AND City = Moscow", &expr) == 0) {
 *       // ... используем expr ...
 *       free_filter_expr(&expr);          // ← обязательно!
 *   }
 *
 *   // Повторный вызов безопасен
 *   free_filter_expr(&expr);              // ничего не произойдёт
 *
 * @warning
 *   - НЕ вызывайте free() на expr->conditions или expr->logic_ops вручную —
 *     это уже сделано внутри функции
 *   - Если вы сами выделяли память для expr (malloc), то free(expr) нужно делать
 *     отдельно после free_filter_expr(&expr)
 *   - После вызова НЕЛЬЗЯ использовать поля expr без повторного заполнения
 *     (все указатели обнулены)
 *
 * @see
 *   - parse_filter_expression() — функция, которая выделяет память в expr
 *   - apply_filter()            — основной потребитель (освобождает после работы)
 *   - row_matches_filter()      — использует expr, но не выделяет память
 */
void free_filter_expr(FilterExpr *expr)
{
    // Если передан NULL — ничего не делаем (безопасно)
    if (!expr) {
        return;
    }

    // Освобождаем каждое динамически выделенное поле в условиях
    for (int i = 0; i < expr->cond_count; i++)
    {
        // column и value были выделены через strdup в parse_filter_expression
        free(expr->conditions[i].column);
        free(expr->conditions[i].value);

        // На всякий случай обнуляем (хотя уже не обязательно)
        expr->conditions[i].column = NULL;
        expr->conditions[i].value  = NULL;
    }

    // Освобождаем сам массив условий
    free(expr->conditions);
    expr->conditions = NULL;

    // Освобождаем массив логических операторов (может быть NULL)
    free(expr->logic_ops);
    expr->logic_ops = NULL;

    // Обнуляем всю структуру для безопасности и предсказуемости
    // (cond_count = 0, negated = 0, все указатели NULL)
    memset(expr, 0, sizeof(FilterExpr));
}

/**
 * @brief Функция сравнения двух double для qsort (callback)
 *
 * Простое сравнение двух чисел с плавающей точкой.
 * Возвращает отрицательное значение, если da < db,
 * положительное если da > db, и ноль если равны.
 *
 * @param a  Указатель на первое значение double
 * @param b  Указатель на второе значение double
 * @return
 *   -1   — da < db
 *    0   — da == db
 *    1   — da > db
 *
 * @note
 *   - Очень простая и быстрая реализация
 *   - Используется для сортировки массивов чисел с плавающей точкой
 *   - Не учитывает NaN и бесконечности (стандартное поведение qsort с ними неопределённое)
 *
 * @example
 *   double values[] = {3.14, 1.0, 2.718, 0.0};
 *   qsort(values, 4, sizeof(double), compare_double);
 *   // После сортировки: 0.0, 1.0, 2.718, 3.14
 *
 * @warning
 *   - Предполагается, что передаются именно double (не float!)
 *   - При наличии NaN поведение может быть непредсказуемым
 *   - Для стабильной сортировки лучше использовать стабильные алгоритмы
 *
 * @see qsort() из <stdlib.h>
 */
int compare_double(const void *a, const void *b)
{
    // Извлекаем значения double из указателей
    double da = *(const double *)a;
    double db = *(const double *)b;

    // Простое сравнение
    if (da < db) {
        return -1;
    }
    if (da > db) {
        return 1;
    }

    // Равны (включая +0 и -0 в IEEE 754)
    return 0;
}

/**
 * Форматирует целое число с пробелами как разделителями тысяч
 * Пример: 4351235 → "4 351 235"
 *         1000000  → "1 000 000"
 *         123      → "123"
 */
void format_number_with_spaces(long long num, char *buf, size_t bufsize)
{
    if (bufsize < 2) {
        buf[0] = '\0';
        return;
    }

    char temp[32];              // достаточно для long long
    int len = snprintf(temp, sizeof(temp), "%lld", num);
    if (len < 0 || len >= (int)sizeof(temp)) {
        strncpy(buf, "?", bufsize);
        buf[bufsize-1] = '\0';
        return;
    }

    int out_pos = 0;
    int digits = 0;

    // Идём с конца к началу
    for (int i = len - 1; i >= 0; i--) {
        if (digits > 0 && digits % 3 == 0 && out_pos < (int)bufsize - 1) {
            buf[out_pos++] = ' ';
        }
        if (out_pos < (int)bufsize - 1) {
            buf[out_pos++] = temp[i];
        }
        digits++;
    }

    buf[out_pos] = '\0';

    // Разворачиваем строку обратно
    int left = 0, right = out_pos - 1;
    while (left < right) {
        char t = buf[left];
        buf[left] = buf[right];
        buf[right] = t;
        left++;
        right--;
    }
}

/**
 * Обрезает строку до max_width символов с добавлением … в конце,
 * если строка была длиннее. Возвращает новую выделенную строку.
 * Если строка NULL или пустая → возвращает пустую строку "".
 */
char *truncate_for_display(const char *str, int max_width)
{
    if (!str || !*str) {
        return strdup("");
    }

    if (max_width <= 0) {
        return strdup("");
    }

    size_t len = strlen(str);
    if (len <= (size_t)max_width) {
        return strdup(str);
    }

    // Оставляем место под … (3 символа)
    int keep = max_width;
    if (keep < 1) keep = 1;

    char *result = malloc(max_width + 1);
    if (!result) return strdup(str);  // на случай ошибки

    strncpy(result, str, keep);
    //strcpy(result + keep, "...");

    result[max_width] = '\0';
    return result;
}

char *clean_column_name(const char *raw)
{
    if (!raw || !*raw) return strdup("");

    // копируем и убираем trailing whitespace + \r\n + неразрывные пробелы
    char *s = strdup(raw);
    if (!s) return strdup(raw);

    // убираем конец
    size_t len = strlen(s);
    while (len > 0) {
        unsigned char c = (unsigned char)s[len-1];
        if (c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == 0xA0) {
            s[--len] = '\0';
        } else {
            break;
        }
    }

    // убираем начало (на всякий случай)
    char *start = s;
    while (*start == ' ' || *start == '\t' || (unsigned char)*start == 0xA0) {
        start++;
    }

    if (start != s) {
        memmove(s, start, strlen(start) + 1);
    }

    return s;
}

/**
 * Парсит одну строку CSV по правилам RFC 4180 (простая реализация).
 * Возвращает массив выделенных полей (нужно free каждое поле и сам массив).
 *
 * @param line      Входная строка (из файла или кэша)
 * @param out_count [out] Сколько полей получилось
 * @return          Массив char* (NULL-terminated), каждый элемент — malloc-строка
 *                  При ошибке возвращает NULL, out_count = 0
 */
char **parse_csv_line(const char *line, int *out_count)
{
    if (!line || !out_count) {
        if (out_count) *out_count = 0;
        return NULL;
    }

    *out_count = 0;

    // Считаем приблизительное количество полей (для начального выделения)
    int max_fields = 64; // начальный размер, потом realloc
    char **fields = malloc(max_fields * sizeof(char *));
    if (!fields) return NULL;

    const char *p = line;
    int field_count = 0;
    char field_buf[MAX_LINE_LEN];
    int buf_pos;
    int in_quotes;

    while (*p)
    {
        buf_pos = 0;
        in_quotes = 0;
        field_buf[0] = '\0';

        // Пропускаем начальные пробелы (но не сам разделитель — важно для TSV)
        while ((*p == ' ' || *p == '\t') && *p != csv_delimiter) p++;

        while (*p)
        {
            if (*p == '"')
            {
                if (in_quotes)
                {
                    // Закрывающая кавычка
                    if (*(p + 1) == '"')
                    {
                        // Экранированная "" → пишем одну "
                        if (buf_pos < MAX_LINE_LEN - 1)
                            field_buf[buf_pos++] = '"';
                        p += 2;
                        continue;
                    }
                    else
                    {
                        // Конец кавычек
                        in_quotes = 0;
                        p++;
                        continue;
                    }
                }
                else
                {
                    // Начало кавычек
                    in_quotes = 1;
                    p++;
                    continue;
                }
            }

            if (*p == csv_delimiter && !in_quotes)
            {
                p++; // переходим за разделитель
                break;
            }

            // Обычный символ
            if (buf_pos < MAX_LINE_LEN - 1)
            {
                field_buf[buf_pos++] = *p;
            }
            p++;
        }

        field_buf[buf_pos] = '\0';

        // Добавляем поле в массив
        if (field_count >= max_fields)
        {
            max_fields *= 2;
            char **new_fields = realloc(fields, max_fields * sizeof(char *));
            if (!new_fields)
            {
                // Ошибка — освобождаем всё
                for (int k = 0; k < field_count; k++) free(fields[k]);
                free(fields);
                *out_count = 0;
                return NULL;
            }
            fields = new_fields;
        }

        fields[field_count] = strdup(field_buf);
        if (!fields[field_count])
        {
            // Ошибка памяти — чистим
            for (int k = 0; k < field_count; k++) free(fields[k]);
            free(fields);
            *out_count = 0;
            return NULL;
        }

        field_count++;

        // Пропускаем пробелы после разделителя (но не сам разделитель)
        while ((*p == ' ' || *p == '\t') && *p != csv_delimiter) p++;
    }

    // Последнее поле (если строка не закончилась запятой)
    if (buf_pos > 0 || field_count > 0)
    {
        // Если мы уже добавили — ок
        // Если нет — добавляем пустое, если была запятая в конце
        if (*(p - 1) == csv_delimiter && buf_pos == 0)
        {
            fields[field_count] = strdup("");
            field_count++;
        }
    }

    *out_count = field_count;
    return fields;
}

/**
 * Освобождает массив полей, возвращённый parse_csv_line().
 */
void free_csv_fields(char **fields, int count)
{
    if (!fields) return;
    for (int i = 0; i < count; i++) free(fields[i]);
    free(fields);
}

/**
 * Собирает CSV/TSV/PSV строку из массива полей (RFC 4180).
 * Поля, содержащие разделитель, кавычку или перевод строки, оборачиваются в "...".
 * NULL-поля трактуются как пустая строка.
 * Возвращает malloc-строку БЕЗ trailing \n. Нужно free().
 */
char *build_csv_line(char **fields, int count, char delimiter)
{
    if (!fields || count <= 0) return strdup("");

    // Считаем максимальный размер (worst case: каждый символ — кавычка)
    size_t total = (size_t)count + 2; // разделители + \0
    for (int i = 0; i < count; i++) {
        if (fields[i]) total += strlen(fields[i]) * 2 + 2;
    }

    char *result = malloc(total);
    if (!result) return NULL;

    char *p = result;
    for (int i = 0; i < count; i++) {
        if (i > 0) *p++ = delimiter;

        const char *f = fields[i] ? fields[i] : "";

        // Нужны ли кавычки?
        int needs_quotes = 0;
        for (const char *s = f; *s; s++) {
            if (*s == delimiter || *s == '"' || *s == '\n' || *s == '\r') {
                needs_quotes = 1;
                break;
            }
        }

        if (needs_quotes) {
            *p++ = '"';
            for (const char *s = f; *s; s++) {
                if (*s == '"') *p++ = '"'; // экранирование: "" → ""
                *p++ = *s;
            }
            *p++ = '"';
        } else {
            size_t len = strlen(f);
            memcpy(p, f, len);
            p += len;
        }
    }
    *p = '\0';
    return result;
}
/**
 * ui_draw.h
 *
 * Интерфейс модуля отрисовки интерфейса таблицы и элементов UI
 * Содержит функции рисования меню, ячеек, рамок, заголовков, тела таблицы и статус-бара
 */

#ifndef UI_DRAW_H
#define UI_DRAW_H

#include "csvview_defs.h"   // RowIndex, globals (cur_col, cur_display_row и т.д.)
#include "utils.h"          // col_letter, format_cell_value и т.д.

// ────────────────────────────────────────────────
// Публичные функции модуля
// ────────────────────────────────────────────────

/**
 * @brief Рисует строку меню внизу экрана
 *
 * @param y         Вертикальная позиция (обычно LINES - 1)
 * @param x         Горизонтальная позиция (обычно 0)
 * @param w         Ширина экрана (COLS)
 * @param menu_type 1 = основное меню, 2 = контекстное меню
 */
void draw_menu(int y, int x, int w, int menu_type);

/**
 * @brief Рисует увеличенное окно с содержимым одной ячейки
 *
 * @param y           Вертикальная позиция верхнего левого угла
 * @param x           Горизонтальная позиция верхнего левого угла
 * @param col_name    Имя столбца
 * @param row_num     Номер строки (видимый)
 * @param raw_content Исходное содержимое ячейки
 * @param width       Ширина окна
 */
void draw_cell_view(int y, const char *col_name, int row_num, const char *raw_content, int width);

/**
 * @brief Рисует внешнюю рамку таблицы
 *
 * @param top     Вертикальная позиция верхней линии
 * @param height  Высота рамки
 * @param width   Ширина рамки
 */
void draw_table_border(int top, int height, int width);

/**
 * @brief Рисует строку заголовков таблицы
 *
 * Подсвечивает текущий столбец и стрелку сортировки (если есть)
 *
 * @param top          Вертикальная позиция строки заголовков
 * @param offset       Не используется (оставлен для совместимости)
 * @param visible_cols Количество видимых столбцов
 * @param left_col     Первый видимый столбец
 * @param cur_col      Текущий (выделенный) столбец
 */
void draw_table_headers(int top, int offset, int visible_cols,
                        int left_col, int cur_col);

/**
 * @brief Рисует тело таблицы (видимую часть строк)
 *
 * Поддерживает:
 *   - подсветку текущей строки/столбца/ячейки
 *   - форматирование ячеек (format_cell_value)
 *   - ленивую загрузку строк
 *   - горизонтальную прокрутку
 *
 * @param top            Вертикальная позиция начала тела
 * @param offset         Не используется (оставлен для совместимости)
 * @param visible_rows   Количество видимых строк
 * @param top_display_row Первый видимый индекс
 * @param cur_display_row Текущий видимый индекс курсора
 * @param cur_col        Текущий столбец
 * @param left_col       Первый видимый столбец
 * @param visible_cols   Количество видимых столбцов
 * @param rows           Массив индексов строк
 * @param f              FILE* исходного файла
 * @param row_count      Общее количество строк
 */
void draw_table_body(int top, int offset, int visible_rows,
                     int top_display_row, int cur_display_row, int cur_col,
                     int left_col, int visible_cols,
                     RowIndex *rows, FILE *f, int row_count);

/**
 * @brief Рисует статус-бар внизу экрана
 *
 * @param y           Вертикальная позиция (обычно LINES - 1)
 * @param x           Горизонтальная позиция (обычно 0)
 * @param filename    Имя открытого файла
 * @param row_count   Количество строк
 * @param size_str    Размер файла (строка, например "неизв.")
 */
void draw_status_bar(int y, int x, const char *filename,
                     int row_count, const char *size_str);

/**
 * @brief Показывает окно со списком сохранённых фильтров
 *
 * Отображает список всех сохранённых фильтров (`saved_filters[]`).
 * Поддерживает:
 *   - навигацию ↑↓
 *   - Enter — вставить фильтр в filter_query и применить
 *   - Shift+Enter — применить без вставки (если нужно)
 *   - D/d — удалить выбранный фильтр (из памяти и файла .csvf)
 *   - Esc/q — закрыть окно
 *
 * @param csv_filename  Имя исходного CSV-файла (для .csvf)
 *
 * @note
 *   - Если сохранённых фильтров нет — показывает сообщение и выходит
 *   - Окно центрируется, размер адаптируется под количество фильтров
 *   - После выбора/применения вызывает apply_filter() и обновляет статус-бар
 *   - Удаление фильтра перезаписывает весь .csvf-файл (с сохранением других строк)
 *
 * @warning
 *   - Зависит от глобальных: saved_filters, saved_filter_count, filter_query,
 *     in_filter_mode, filter_active, rows, f, row_count
 *   - Перезаписывает .csvf-файл при удалении — осторожно с большими файлами
 *   - Не проверяет права на запись в файл
 *
 * @see
 *   - apply_filter() — применяется после выбора фильтра
 *   - save_filter() / load_saved_filters() — работа с сохранением
 */
void show_saved_filters_window(const char *csv_filename);

// -----------------------------------------------------------------------------
// Получает содержимое ячейки по номеру столбца (cur_col) из строки
// Возвращает новую строку (нужно free), всегда корректно обрабатывает ,, и "текст, с запятой"
// -----------------------------------------------------------------------------
char *get_cell_content(const char *line, int target_col);

/**
 * @brief Рисует один кадр спиннера [ |/-\ ] в правом углу статус-бара
 *
 * Вызывать каждые ~5000 строк во время длительных операций.
 * Анимирует символы | / - \ по кругу.
 */
void spinner_tick(void);

/**
 * @brief Убирает спиннер (стирает символы)
 *
 * Вызывать после завершения длительной операции.
 */
void spinner_clear(void);

/**
 * @brief Показывает окно просмотра строк-комментариев (строки начинающиеся с '#')
 *
 * Доступно когда skip_comments=1 и comment_count>0.
 * Вызывается по клавише '#' в главном виде.
 */
void show_comments_window(void);

#endif /* UI_DRAW_H */
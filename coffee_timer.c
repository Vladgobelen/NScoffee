#include <SDL2/SDL.h>
#include <SDL2/SDL_mixer.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/file.h>
#include <string.h>
#include <errno.h>

#define WINDOW_WIDTH  206
#define WINDOW_HEIGHT 180
#define DEFAULT_TIMER_SECONDS 490
#define DEFAULT_MUSIC_FILE    "/home/diver/Загрузки/Музыка/12/coffee.mp3"
#define LOCK_FILE     "/tmp/coffee_timer.lock"

SDL_Window* window = NULL;
SDL_Renderer* renderer = NULL;
Mix_Music* music = NULL;
int lock_fd = -1;
SDL_Rect cancel_button = {0};

// Прототипы функций
void init_sdl();
void cleanup();
void render_progress(float progress);
int try_get_lock();
void check_exit_signal();
void draw_char(int x, int y, char c);
void draw_string(int x, int y, const char* str);

// Растровые данные для цифр и символа %
static const Uint8 font_data[11][5] = {
    {0x7C, 0x82, 0x82, 0x82, 0x7C}, // 0
    {0x00, 0x42, 0xFE, 0x02, 0x00}, // 1
    {0x42, 0x86, 0x8A, 0x92, 0x62}, // 2
    {0x44, 0x82, 0x92, 0x92, 0x6C}, // 3
    {0x30, 0x50, 0x90, 0xFE, 0x10}, // 4
    {0xE4, 0xA2, 0xA2, 0xA2, 0x9C}, // 5
    {0x7C, 0xA2, 0xA2, 0xA2, 0x1C}, // 6
    {0x80, 0x8E, 0x90, 0xA0, 0xC0}, // 7
    {0x6C, 0x92, 0x92, 0x92, 0x6C}, // 8
    {0x60, 0x92, 0x92, 0x92, 0x7C}, // 9
    {0x62, 0x94, 0x88, 0x94, 0x62}  // %
};

/*----------------------------------------------------------
  Рисование символа с помощью растровой карты
----------------------------------------------------------*/
void draw_char(int x, int y, char c) {
    int index = -1;
    
    if (c >= '0' && c <= '9') {
        index = c - '0';
    } else if (c == '%') {
        index = 10;
    }
    
    if (index < 0 || index > 10) return;
    
    const Uint8* bitmap = font_data[index];
    
    // Рисуем столбец за столбцом
    for (int col = 0; col < 5; col++) {
        Uint8 byte = bitmap[col];
        for (int row = 0; row < 8; row++) {
            if (byte & (1 << (7 - row))) {
                SDL_RenderDrawPoint(renderer, x + col, y + row);
            }
        }
    }
}

/*----------------------------------------------------------
  Рисование строки
----------------------------------------------------------*/
void draw_string(int x, int y, const char* str) {
    for (const char* p = str; *p; p++) {
        draw_char(x, y, *p);
        x += 6;  // Ширина символа + отступ
    }
}

/*----------------------------------------------------------
  Инициализация SDL и аудио
----------------------------------------------------------*/
void init_sdl() {
    // Инициализация SDL
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO) < 0) {
        fprintf(stderr, "SDL init error: %s\n", SDL_GetError());
        exit(1);
    }

    // Инициализация аудио
    if (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 2048) < 0) {
        fprintf(stderr, "Mixer error: %s\n", Mix_GetError());
        SDL_Quit();
        exit(1);
    }

    // Создание окна
    window = SDL_CreateWindow("Coffee Timer", 
                             SDL_WINDOWPOS_CENTERED, 
                             SDL_WINDOWPOS_CENTERED,
                             WINDOW_WIDTH, 
                             WINDOW_HEIGHT, 
                             SDL_WINDOW_SHOWN);
    if (!window) {
        fprintf(stderr, "Window error: %s\n", SDL_GetError());
        Mix_CloseAudio();
        SDL_Quit();
        exit(1);
    }

    // Создание рендерера
    renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer) {
        fprintf(stderr, "Renderer error: %s\n", SDL_GetError());
        SDL_DestroyWindow(window);
        Mix_CloseAudio();
        SDL_Quit();
        exit(1);
    }

    // Инициализация кнопки отмены (под прогресс-баром)
    cancel_button.x = WINDOW_WIDTH - 80;
    cancel_button.y = 70;  // Под прогресс-баром
    cancel_button.w = 70;
    cancel_button.h = 25;
}

/*----------------------------------------------------------
  Отрисовка прогресс-бара и интерфейса
----------------------------------------------------------*/
void render_progress(float progress) {
    // Очистка экрана
    SDL_SetRenderDrawColor(renderer, 30, 30, 30, 255);
    SDL_RenderClear(renderer);

    // Рисуем прогресс-бар
    SDL_SetRenderDrawColor(renderer, 70, 200, 70, 255);
    SDL_Rect bar = {
        .x = 20,
        .y = 40,
        .w = (int)((WINDOW_WIDTH - 40) * progress),
        .h = 20
    };
    SDL_RenderFillRect(renderer, &bar);

    // Отображаем процентный текст
    char progress_text[16];
    snprintf(progress_text, sizeof(progress_text), "%d%%", (int)(progress * 100));
    
    // Устанавливаем белый цвет для текста
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    
    // Рисуем текст внутри прогресс-бара
    int text_x = bar.x + bar.w - 30;
    int text_y = bar.y + 7;  // Центрирование по вертикали
    
    // Если текст слишком близко к краю, смещаем влево
    if (text_x < bar.x + 30) {
        text_x = bar.x + 5;
    }
    
    draw_string(text_x, text_y, progress_text);

    // Рисуем кнопку отмены
    SDL_SetRenderDrawColor(renderer, 180, 50, 50, 255);
    SDL_RenderFillRect(renderer, &cancel_button);
    
    SDL_SetRenderDrawColor(renderer, 230, 80, 80, 255);
    SDL_RenderDrawRect(renderer, &cancel_button);
    
    // Текст на кнопке (русский)
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    draw_string(cancel_button.x + 10, cancel_button.y + 7, "Отмена");
    
    // Выводим на экран
    SDL_RenderPresent(renderer);
}

/*----------------------------------------------------------
  Получение блокировки файла
----------------------------------------------------------*/
int try_get_lock() {
    // Открываем/создаем файл блокировки
    lock_fd = open(LOCK_FILE, O_CREAT | O_RDWR, 0644);
    if (lock_fd == -1) {
        perror("Lock file error");
        return 0;
    }

    // Пытаемся получить эксклюзивную блокировку
    if (flock(lock_fd, LOCK_EX | LOCK_NB) == -1) {
        if (errno == EWOULDBLOCK) {
            // Файл уже заблокирован - записываем команду выхода
            const char exit_cmd[] = "exit";
            write(lock_fd, exit_cmd, sizeof(exit_cmd));
            fsync(lock_fd);
            close(lock_fd);
            printf("Программа уже запущена. Отправлен сигнал выхода.\n");
        } else {
            perror("Lock failed");
            close(lock_fd);
        }
        return 0;
    }

    // Успешно получили блокировку
    ftruncate(lock_fd, 0);
    const char working[] = "working";
    write(lock_fd, working, sizeof(working));
    fsync(lock_fd);
    return 1;
}

/*----------------------------------------------------------
  Проверка команды выхода
----------------------------------------------------------*/
void check_exit_signal() {
    char status[10] = {0};
    lseek(lock_fd, 0, SEEK_SET);
    read(lock_fd, status, sizeof(status));

    if (strcmp(status, "exit") == 0) {
        printf("Получена команда выхода. Завершение...\n");
        cleanup();
        exit(0);
    }
}

/*----------------------------------------------------------
  Очистка ресурсов
----------------------------------------------------------*/
void cleanup() {
    if (music) Mix_FreeMusic(music);
    if (renderer) SDL_DestroyRenderer(renderer);
    if (window) SDL_DestroyWindow(window);
    Mix_CloseAudio();
    SDL_Quit();

    if (lock_fd != -1) {
        flock(lock_fd, LOCK_UN);
        close(lock_fd);
        unlink(LOCK_FILE);
    }
}

/*----------------------------------------------------------
  Главная функция
----------------------------------------------------------*/
int main(int argc, char* argv[]) {
    // Установка значений по умолчанию
    int timer_seconds = DEFAULT_TIMER_SECONDS;
    const char* music_file = DEFAULT_MUSIC_FILE;

    // Обработка аргументов командной строки
    if (argc >= 2) {
        char* endptr;
        long seconds = strtol(argv[1], &endptr, 10);
        
        if (*endptr != '\0' || seconds <= 0) {
            fprintf(stderr, "Invalid timer value. Using default: %d seconds\n", 
                    DEFAULT_TIMER_SECONDS);
        } else {
            timer_seconds = (int)seconds;
        }
    }
    
    if (argc >= 3) {
        music_file = argv[2];
    }

    printf("Using configuration:\n");
    printf("  Timer: %d seconds\n", timer_seconds);
    printf("  Music: %s\n", music_file);

    // Проверка блокировки
    if (!try_get_lock()) {
        return 1;
    }

    // Гарантированная очистка при выходе
    atexit(cleanup);

    // Инициализация
    init_sdl();

    // Загрузка музыки
    music = Mix_LoadMUS(music_file);
    if (!music) {
        fprintf(stderr, "Music load error: %s\n", Mix_GetError());
        return 1;
    }

    // Основной цикл
    int running = 1;
    int seconds = 0;
    SDL_Event event;

    while (running && seconds < timer_seconds) {
        // Обработка событий
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) {
                running = 0;
            }
            else if (event.type == SDL_MOUSEBUTTONDOWN) {
                int mouse_x = event.button.x;
                int mouse_y = event.button.y;
                
                // Проверка клика по кнопке отмены
                if (mouse_x >= cancel_button.x && 
                    mouse_x <= cancel_button.x + cancel_button.w &&
                    mouse_y >= cancel_button.y && 
                    mouse_y <= cancel_button.y + cancel_button.h) {
                    printf("Нажата кнопка отмены\n");
                    running = 0;
                }
            }
        }

        // Проверка команды выхода
        check_exit_signal();

        // Отрисовка
        render_progress((float)seconds / timer_seconds);

        // Пауза 1 секунда
        SDL_Delay(1000);
        seconds++;
    }

    // Воспроизведение музыки только если таймер завершился
    if (running && seconds >= timer_seconds) {
        Mix_PlayMusic(music, 1);
        while (Mix_PlayingMusic()) {
            SDL_Delay(100);
            check_exit_signal();
        }
    }

    return 0;
}